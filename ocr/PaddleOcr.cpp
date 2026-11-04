// input: Depends on PaddleOcr.h and ONNX Runtime, loaded with dlopen.
// output: Implements detection, cutting out, recognition and reading order.
// pos: The engine of mpaste-ocr.
// update: If I change, update this header block and my folder README.md.
#include "PaddleOcr.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

using PaddleOcrImage::Bgr;
using PaddleOcrImage::Point;

namespace {
// Detection, as PaddleOCR and RapidOCR set it.
constexpr float kDetThresh = 0.3f;     // a pixel is text above this
constexpr float kBoxThresh = 0.5f;     // a box is kept above this mean score
constexpr float kUnclipRatio = 1.6f;   // how far a box is grown past its core
constexpr int kDetMinSide = 960;       // the shorter side is brought to this
constexpr int kDetMaxSide = 2000;      // and the longer one kept below this
constexpr int kRecHeight = 48;
constexpr int kRecMaxWidth = 3200;

std::string firstName(Ort::Session &session, bool input) {
    Ort::AllocatorWithDefaultOptions allocator;
    auto name = input ? session.GetInputNameAllocated(0, allocator) : session.GetOutputNameAllocated(0, allocator);
    return name.get();
}

// The picture inside a quadrilateral (corners clockwise from the top left),
// sampled into width x height as a CHW float tensor, (x - mean) * scale per
// channel. Bilinear, averaging a few samples per pixel when it shrinks, so a
// large picture does not alias; edges repeat outward.
std::vector<float> sampleTensor(const Bgr &src, const std::array<Point, 4> &q, int width, int height,
                                const float mean[3], const float scale[3]) {
    std::vector<float> out(std::size_t(3) * width * height);
    const float spanX = std::max(std::hypot(q[1].x - q[0].x, q[1].y - q[0].y), std::hypot(q[2].x - q[3].x, q[2].y - q[3].y));
    const float spanY = std::max(std::hypot(q[3].x - q[0].x, q[3].y - q[0].y), std::hypot(q[2].x - q[1].x, q[2].y - q[1].y));
    const int kx = std::clamp(int(spanX / width), 1, 4);
    const int ky = std::clamp(int(spanY / height), 1, 4);
    auto pixel = [&src](int x, int y, int c) {
        x = std::clamp(x, 0, src.width - 1);
        y = std::clamp(y, 0, src.height - 1);
        return float(src.data[std::size_t(y) * src.stride + std::size_t(x) * 3 + c]);
    };
    const std::size_t plane = std::size_t(width) * height;
    for (int oy = 0; oy < height; ++oy) {
        for (int ox = 0; ox < width; ++ox) {
            float sum[3] = {0, 0, 0};
            for (int sy = 0; sy < ky; ++sy) {
                for (int sx = 0; sx < kx; ++sx) {
                    const float u = (ox + (sx + 0.5f) / kx) / width;
                    const float v = (oy + (sy + 0.5f) / ky) / height;
                    // Bilinear blend of the corners, then of the pixels around it.
                    const float px = (1 - u) * (1 - v) * q[0].x + u * (1 - v) * q[1].x + u * v * q[2].x + (1 - u) * v * q[3].x - 0.5f;
                    const float py = (1 - u) * (1 - v) * q[0].y + u * (1 - v) * q[1].y + u * v * q[2].y + (1 - u) * v * q[3].y - 0.5f;
                    const int x0 = int(std::floor(px)), y0 = int(std::floor(py));
                    const float fx = px - x0, fy = py - y0;
                    for (int c = 0; c < 3; ++c) {
                        const float top = pixel(x0, y0, c) * (1 - fx) + pixel(x0 + 1, y0, c) * fx;
                        const float bottom = pixel(x0, y0 + 1, c) * (1 - fx) + pixel(x0 + 1, y0 + 1, c) * fx;
                        sum[c] += top * (1 - fy) + bottom * fy;
                    }
                }
            }
            for (int c = 0; c < 3; ++c) {
                out[c * plane + std::size_t(oy) * width + ox] = (sum[c] / (kx * ky) - mean[c]) * scale[c];
            }
        }
    }
    return out;
}

float cross(const Point &o, const Point &a, const Point &b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Convex hull, Andrew's monotone chain.
std::vector<Point> convexHull(std::vector<Point> pts) {
    std::sort(pts.begin(), pts.end(), [](const Point &a, const Point &b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    if (pts.size() < 3) return pts;
    std::vector<Point> hull(2 * pts.size());
    std::size_t k = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
        hull[k++] = pts[i];
    }
    for (std::size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i - 1]) <= 0) --k;
        hull[k++] = pts[i - 1];
    }
    hull.resize(k - 1);
    return hull;
}

// The smallest rectangle around the points, as a centre, a unit direction
// along one side, and the side lengths: rotating calipers over the hull.
struct RotatedRect {
    Point centre;
    Point axis{1, 0};   // along `width`
    float width = 0, height = 0;
};

RotatedRect minAreaRect(const std::vector<Point> &points) {
    const std::vector<Point> hull = convexHull(points);
    RotatedRect best;
    float bestArea = -1;
    for (std::size_t i = 0; i < hull.size(); ++i) {
        const Point &a = hull[i];
        const Point &b = hull[(i + 1) % hull.size()];
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        if (len < 1e-6f) continue;
        const Point u{(b.x - a.x) / len, (b.y - a.y) / len};
        const Point v{-u.y, u.x};
        float minU = 1e30f, maxU = -1e30f, minV = 1e30f, maxV = -1e30f;
        for (const Point &p : hull) {
            const float pu = p.x * u.x + p.y * u.y, pv = p.x * v.x + p.y * v.y;
            minU = std::min(minU, pu); maxU = std::max(maxU, pu);
            minV = std::min(minV, pv); maxV = std::max(maxV, pv);
        }
        const float area = (maxU - minU) * (maxV - minV);
        if (bestArea < 0 || area < bestArea) {
            bestArea = area;
            const float cu = (minU + maxU) / 2, cv = (minV + maxV) / 2;
            best.centre = {cu * u.x + cv * v.x, cu * u.y + cv * v.y};
            best.axis = u;
            best.width = maxU - minU;
            best.height = maxV - minV;
        }
    }
    if (bestArea < 0 && !hull.empty()) {   // all one point
        best.centre = hull.front();
    }
    return best;
}

// The rectangle's corners clockwise from the top left (of the two leftmost,
// the higher first).
std::array<Point, 4> orderedCorners(const RotatedRect &r) {
    const Point u{r.axis.x * r.width / 2, r.axis.y * r.width / 2};
    const Point v{-r.axis.y * r.height / 2, r.axis.x * r.height / 2};
    std::array<Point, 4> p = {Point{r.centre.x - u.x - v.x, r.centre.y - u.y - v.y},
                              Point{r.centre.x + u.x - v.x, r.centre.y + u.y - v.y},
                              Point{r.centre.x + u.x + v.x, r.centre.y + u.y + v.y},
                              Point{r.centre.x - u.x + v.x, r.centre.y - u.y + v.y}};
    std::sort(p.begin(), p.end(), [](const Point &a, const Point &b) { return a.x < b.x; });
    if (p[0].y > p[1].y) std::swap(p[0], p[1]);
    if (p[2].y > p[3].y) std::swap(p[2], p[3]);
    return {p[0], p[2], p[3], p[1]};
}

// The box's mean score over the probability map, as DB does.
float boxScore(const std::vector<float> &prob, int w, int h, const std::array<Point, 4> &q) {
    float minX = q[0].x, maxX = q[0].x, minY = q[0].y, maxY = q[0].y;
    for (const Point &p : q) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    const int x0 = std::max(0, int(std::floor(minX))), x1 = std::min(w - 1, int(std::ceil(maxX)));
    const int y0 = std::max(0, int(std::floor(minY))), y1 = std::min(h - 1, int(std::ceil(maxY)));
    double sum = 0;
    int count = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Point c{float(x), float(y)};
            bool inside = true;
            for (int i = 0; i < 4 && inside; ++i) {
                inside = cross(q[i], q[(i + 1) % 4], c) >= -0.5f;
            }
            if (inside) {
                sum += prob[std::size_t(y) * w + x];
                ++count;
            }
        }
    }
    return count > 0 ? float(sum / count) : 0.0f;
}

std::vector<std::string> splitLines(const std::string &text) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

bool isWide(const std::string &utf8) {
    // CJK and fullwidth forms start at U+2E80 (UTF-8 E2 BA 80).
    const auto c = static_cast<unsigned char>(utf8.empty() ? 0 : utf8[0]);
    return c >= 0xE3 || (c == 0xE2 && utf8.size() > 1 && static_cast<unsigned char>(utf8[1]) >= 0xBA);
}

std::string lastChar(const std::string &s) {
    std::size_t i = s.size();
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
    return i > 0 ? s.substr(i - 1) : std::string();
}

std::string firstChar(const std::string &s) {
    if (s.empty()) return s;
    std::size_t n = 1;
    while (n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) ++n;
    return s.substr(0, n);
}
}  // namespace

bool PaddleOcr::loadRuntime(std::string *error) {
    void *library = nullptr;
    for (const char *name : {"libonnxruntime.so.1", "libonnxruntime.so"}) {
        library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (library) break;
    }
    if (!library) {
        const char *why = dlerror();
        if (error) *error = why ? why : "libonnxruntime.so.1 not found";
        return false;
    }
    using GetApiBase = const OrtApiBase *(*)();
    auto getApiBase = reinterpret_cast<GetApiBase>(dlsym(library, "OrtGetApiBase"));
    const OrtApi *api = getApiBase ? getApiBase()->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) {
        if (error) {
            *error = std::string("ONNX Runtime too old: need API ") + std::to_string(ORT_API_VERSION)
                + (getApiBase ? std::string(", have ") + getApiBase()->GetVersionString() : std::string());
        }
        return false;
    }
    Ort::InitApi(api);
    return true;
}

PaddleOcr::PaddleOcr() = default;
PaddleOcr::~PaddleOcr() = default;

std::string PaddleOcr::load(const std::string &detModel, const std::string &recModel, int threads) {
    try {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "mpaste-ocr");
        options_ = std::make_unique<Ort::SessionOptions>();
        options_->SetIntraOpNumThreads(threads);
        options_->SetInterOpNumThreads(1);
        options_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        det_ = std::make_unique<Ort::Session>(*env_, detModel.c_str(), *options_);
        rec_ = std::make_unique<Ort::Session>(*env_, recModel.c_str(), *options_);
        detInput_ = firstName(*det_, true);
        detOutput_ = firstName(*det_, false);
        recInput_ = firstName(*rec_, true);
        recOutput_ = firstName(*rec_, false);

        Ort::AllocatorWithDefaultOptions allocator;
        Ort::ModelMetadata meta = rec_->GetModelMetadata();
        auto character = meta.LookupCustomMetadataMapAllocated("character", allocator);
        if (!character) {
            return "the recognition model has no dictionary (metadata \"character\")";
        }
        keys_ = splitLines(character.get());
        keys_.insert(keys_.begin(), std::string());   // the CTC blank
        keys_.push_back(" ");                         // PaddleOCR adds the space last
    } catch (const Ort::Exception &e) {
        return e.what();
    }
    return {};
}

std::vector<PaddleOcr::Quad> PaddleOcr::detect(const Bgr &image, int top, int height) {
    // Scale so the shorter side is kDetMinSide and the longer at most
    // kDetMaxSide, both multiples of 32.
    const int w = image.width, h = height;
    double ratio = double(kDetMinSide) / std::min(w, h);
    if (std::max(w, h) * ratio > kDetMaxSide) ratio = double(kDetMaxSide) / std::max(w, h);
    const int dw = std::max(32, int(std::round(w * ratio / 32.0)) * 32);
    const int dh = std::max(32, int(std::round(h * ratio / 32.0)) * 32);

    static const float mean[3] = {0.485f * 255, 0.456f * 255, 0.406f * 255};
    static const float scale[3] = {1.0f / (0.229f * 255), 1.0f / (0.224f * 255), 1.0f / (0.225f * 255)};
    const std::array<Point, 4> whole = {Point{0, float(top)}, Point{float(w), float(top)},
                                        Point{float(w), float(top + h)}, Point{0, float(top + h)}};
    std::vector<float> input = sampleTensor(image, whole, dw, dh, mean, scale);
    const std::array<int64_t, 4> shape{1, 3, dh, dw};
    auto memory = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());
    const char *inName = detInput_.c_str();
    const char *outName = detOutput_.c_str();
    auto outputs = det_->Run(Ort::RunOptions{nullptr}, &inName, &tensor, 1, &outName, 1);
    const auto outShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    const int oh = int(outShape[2]), ow = int(outShape[3]);
    const float *probData = outputs[0].GetTensorData<float>();
    const std::vector<float> prob(probData, probData + std::size_t(ow) * oh);

    // Text pixels, grown by one to the right and down (DB's 2 x 2 dilation),
    // then each 8-connected region on its own.
    std::vector<std::uint8_t> mask(std::size_t(ow) * oh, 0);
    for (int y = 0; y < oh; ++y) {
        for (int x = 0; x < ow; ++x) {
            if (prob[std::size_t(y) * ow + x] > kDetThresh) {
                for (int dy = 0; dy <= 1 && y + dy < oh; ++dy)
                    for (int dx = 0; dx <= 1 && x + dx < ow; ++dx) mask[std::size_t(y + dy) * ow + x + dx] = 1;
            }
        }
    }

    const float sx = float(w) / ow, sy = float(h) / oh;
    std::vector<Quad> boxes;
    std::vector<int> stack;
    std::vector<Point> region;
    for (int start = 0; start < ow * oh; ++start) {
        if (mask[start] != 1) continue;
        region.clear();
        stack.assign(1, start);
        mask[start] = 2;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            const int x = i % ow, y = i / ow;
            region.push_back({float(x), float(y)});
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= ow || ny >= oh) continue;
                    const int n = ny * ow + nx;
                    if (mask[n] == 1) {
                        mask[n] = 2;
                        stack.push_back(n);
                    }
                }
            }
        }
        if (region.size() < 3) continue;
        const RotatedRect core = minAreaRect(region);
        if (std::max(core.width, core.height) < 3) continue;
        if (boxScore(prob, ow, oh, orderedCorners(core)) < kBoxThresh) continue;
        // Grow the box past its core by area * ratio / perimeter on every
        // side; for a rectangle that is what DB's polygon offset gives.
        // Pixel centres span one less than the pixels do.
        const float cw = core.width + 1, ch = core.height + 1;
        const float grow = cw * ch * kUnclipRatio / (2 * (cw + ch));
        RotatedRect grown = core;
        grown.width = cw + 2 * grow;
        grown.height = ch + 2 * grow;
        if (std::max(grown.width, grown.height) < 5) continue;
        Quad box;
        for (Point p : orderedCorners(grown)) {
            p.x = std::clamp((p.x + 0.5f) * sx, 0.0f, float(w));
            p.y = std::clamp((p.y + 0.5f) * sy, 0.0f, float(h)) + float(top);
            box.push_back(p);
        }
        boxes.push_back(box);
    }
    return boxes;
}

std::string PaddleOcr::recognize(const Bgr &image, const Quad &c) {
    const float bw = std::max(std::hypot(c[1].x - c[0].x, c[1].y - c[0].y), std::hypot(c[2].x - c[3].x, c[2].y - c[3].y));
    const float bh = std::max(std::hypot(c[3].x - c[0].x, c[3].y - c[0].y), std::hypot(c[2].x - c[1].x, c[2].y - c[1].y));
    if (bw < 2 || bh < 2) return {};
    // A tall box is a vertical line, read turned a quarter left.
    std::array<Point, 4> q = {c[0], c[1], c[2], c[3]};
    float lineW = bw, lineH = bh;
    if (bh >= bw * 1.5f) {
        q = {c[1], c[2], c[3], c[0]};
        std::swap(lineW, lineH);
    }
    const int width = std::clamp(int(std::ceil(lineW * kRecHeight / lineH)), 8, kRecMaxWidth);
    static const float mean[3] = {127.5f, 127.5f, 127.5f};
    static const float scale[3] = {1.0f / 127.5f, 1.0f / 127.5f, 1.0f / 127.5f};
    std::vector<float> input = sampleTensor(image, q, width, kRecHeight, mean, scale);
    const std::array<int64_t, 4> shape{1, 3, kRecHeight, width};
    auto memory = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());
    const char *inName = recInput_.c_str();
    const char *outName = recOutput_.c_str();
    auto outputs = rec_->Run(Ort::RunOptions{nullptr}, &inName, &tensor, 1, &outName, 1);
    const auto outShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    const std::size_t steps = std::size_t(outShape[1]), classes = std::size_t(outShape[2]);
    const float *data = outputs[0].GetTensorData<float>();

    // CTC: the best class at each step, repeats and blanks dropped.
    std::string text;
    std::size_t last = 0;
    for (std::size_t t = 0; t < steps; ++t) {
        const float *row = data + t * classes;
        const std::size_t best = std::size_t(std::max_element(row, row + classes) - row);
        if (best != 0 && best != last && best < keys_.size()) {
            text += keys_[best];
        }
        last = best;
    }
    return text;
}

std::string PaddleOcr::read(const Bgr &image) {
    if (image.width <= 0 || image.height <= 0 || !det_ || !rec_) return {};

    // A long screenshot is read in slices about one and a half times as tall
    // as it is wide, each overlapping the next; a line found in two is kept
    // from the slice it sits more inside of.
    const int sliceHeight = std::max(image.width * 3 / 2, 1024);
    const int overlap = std::min(160, sliceHeight / 4);
    std::vector<Line> lines;
    for (int top = 0; top < image.height; top += sliceHeight - overlap) {
        const int bottom = std::min(image.height, top + sliceHeight);
        for (const Quad &box : detect(image, top, bottom - top)) {
            float minX = box[0].x, maxX = box[0].x, minY = box[0].y, maxY = box[0].y;
            for (const Point &p : box) {
                minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            }
            const float centre = (minY + maxY) / 2;
            // In the overlap above this slice: the slice before had it.
            if (top > 0 && centre < top + overlap / 2.0f) continue;
            // In the overlap below: the next slice has it, unless this is the last.
            if (bottom < image.height && centre >= bottom - overlap / 2.0f) continue;
            lines.push_back({minX, minY, maxX - minX, maxY - minY, box, {}});
        }
        if (bottom >= image.height) break;
    }

    for (Line &line : lines) {
        line.text = recognize(image, line.corners);
    }
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const Line &l) { return l.text.empty(); }), lines.end());

    // Reading order: top to bottom; boxes whose middles share a band are one
    // line, left to right, joined with a space where words are Latin.
    std::sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) { return a.y < b.y; });
    std::string out;
    std::size_t i = 0;
    while (i < lines.size()) {
        std::vector<const Line *> row{&lines[i]};
        const float mid = lines[i].y + lines[i].h / 2;
        const float band = lines[i].h / 2;
        std::size_t j = i + 1;
        while (j < lines.size() && std::abs(lines[j].y + lines[j].h / 2 - mid) < band) {
            row.push_back(&lines[j]);
            ++j;
        }
        std::sort(row.begin(), row.end(), [](const Line *a, const Line *b) { return a->x < b->x; });
        std::string text;
        for (const Line *part : row) {
            if (!text.empty() && !isWide(lastChar(text)) && !isWide(firstChar(part->text))) text += ' ';
            text += part->text;
        }
        if (!out.empty()) out += '\n';
        out += text;
        i = j;
    }
    return out;
}
