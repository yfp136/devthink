// jpeg_codec.cpp - 自包含 baseline JFIF JPEG 编码器（RGB24 -> JPEG）。
//
// 设计目标：
//   * 无任何第三方依赖，纯 C++20，可在 macOS / Windows / Linux 上编译。
//   * 固定质量因子 85，YCbCr 4:2:0 二次采样，标准 Annex K Huffman 表。
//   * 全整数定点运算，输出字节流确定可复现（同输入必同输出）。
//   * 非法输入（空指针、非正尺寸、尺寸溢出）一律返回空 vector。
//
// 布局：SOI | APP0(JFIF) | DQT x2 | DHT x4 | SOF0 | SOS | 熵编码数据 | EOI。

#include "jpeg_codec.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace sm {
namespace platform {
namespace {

// ---------------------------------------------------------------------------
// 常量表
// ---------------------------------------------------------------------------

// 标准 Annex K 基准亮度量化表（自然序 8x8，行主序）。质量 85 缩放后的值。
// 缩放规则（libjpeg 兼容）：scale = 200 - 2*Q；Q85 -> scale=30；
// entry = clamp(1..255, (v*scale + 50) / 100)。
static const uint8_t kQuantLuma[64] = {
    5, 3, 3, 5, 7, 12, 15, 18,  4, 4, 4, 6, 8, 17, 18, 17,
    4, 4, 5, 7, 12, 17, 21, 17,  4, 5, 7, 9, 15, 26, 24, 19,
    5, 7, 11, 17, 20, 33, 31, 23,  7, 11, 17, 19, 24, 31, 34, 28,
    15, 19, 23, 26, 31, 36, 36, 30, 22, 28, 29, 29, 34, 30, 31, 30};

static const uint8_t kQuantChroma[64] = {
    5, 5, 7, 14, 30, 30, 30, 30,  5, 6, 8, 20, 30, 30, 30, 30,
    7, 8, 17, 30, 30, 30, 30, 30,  14, 20, 30, 30, 30, 30, 30, 30,
    30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
    30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30};

// 整数 1D DCT 基：round(8192 * C(u) * cos((2x+1)*u*PI/16))，C(0)=1/sqrt(2)。
static const int16_t kCos[8][8] = {
    {5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793},
    {8035, 6811, 4551, 1598, -1598, -4551, -6811, -8035},
    {7568, 3135, -3135, -7568, -7568, -3135, 3135, 7568},
    {6811, -1598, -8035, -4551, 4551, 8035, 1598, -6811},
    {5793, -5793, -5793, 5793, 5793, -5793, -5793, 5793},
    {4551, -8035, 1598, 6811, -6811, -1598, 8035, -4551},
    {3135, -7568, 7568, -3135, -3135, 7568, -7568, 3135},
    {1598, -4551, 6811, -8035, 8035, -6811, 4551, -1598}};

// 自然序坐标 -> zigzag 扫描次序（JPEG Annex A，索引为自然序的展开序号）。
static const uint8_t kZigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// 标准 Huffman 表（JPEG Annex K.3，取自 libjpeg-turbo jstdhuff.c）。
// bits[n] 表示码长为 n 的符号个数（bits[0] 恒为 0）。
static const uint8_t kBitsDcLum[17] = {0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t kBitsDcChr[17] = {0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t kBitsAcLum[17] = {0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static const uint8_t kBitsAcChr[17] = {0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};

static const uint8_t kValDc[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const uint8_t kValAcLum[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa};

static const uint8_t kValAcChr[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa};

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------

struct HCode {
    uint16_t code;  // canonical Huffman 码（左对齐前 len 位有效）
    uint8_t len;
};

// 由标准 bits/val 构造 canonical Huffman 码表。
void BuildHuff(const uint8_t* bits, const uint8_t* vals, int nvals,
               HCode out[256]) {
    int code = 0;
    int k = 0;
    for (int len = 1; len <= 16; ++len) {
        for (int i = 0; i < bits[len] && k < nvals; ++i) {
            out[vals[k++]] = {static_cast<uint16_t>(code), static_cast<uint8_t>(len)};
            ++code;
        }
        code <<= 1;
    }
}

// 有符号右移（向最近整数取整）。
int64_t RShiftRound(int64_t v, int sh) {
    if (v >= 0) return (v + (int64_t(1) << (sh - 1))) >> sh;
    return -(((-v) + (int64_t(1) << (sh - 1))) >> sh);
}

// 有符号除法（向最近整数取整）。
int QuantRound(int64_t v, int q) {
    if (v >= 0) return static_cast<int>((v + q / 2) / q);
    return -static_cast<int>(((-v) + q / 2) / q);
}

// 受限 clamp 到 [0, 255]。
inline uint8_t Clamp255(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// RGB -> YCbCr（JFIF 全范围，定点 16 位）。常量保证中性色（R=G=B）时
// Cb=Cr=128 恰好成立。
inline void RgbToYcc(uint8_t r, uint8_t g, uint8_t b, uint8_t& y, uint8_t& cb,
                     uint8_t& cr) {
    const int rr = r, gg = g, bb = b;
    const int yy = (19595 * rr + 38470 * gg + 7471 * bb + 32768) >> 16;
    const int cbb = ((-11059) * rr + (-21709) * gg + 32768 * bb + 32768) >> 16;
    const int crr = (32768 * rr + (-27439) * gg + (-5329) * bb + 32768) >> 16;
    y = Clamp255(yy);
    cb = Clamp255(cbb + 128);
    cr = Clamp255(crr + 128);
}

// ---------------------------------------------------------------------------
// 位流写出器（含 0xFF 填充）
// ---------------------------------------------------------------------------

class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}

    // 低位的最后 `len` 位有效。
    void PutBits(uint32_t code, int len) {
        if (len <= 0) return;
        acc_ |= static_cast<uint64_t>(code) << (64 - nbits_ - len);
        nbits_ += len;
        while (nbits_ >= 8) {
            uint8_t byte = static_cast<uint8_t>(acc_ >> 56);
            acc_ <<= 8;
            nbits_ -= 8;
            out_.push_back(byte);
            if (byte == 0xFF) out_.push_back(0x00);  // 字节填充
        }
    }

    // 在熵数据结束时对齐到字节边界：先补一个 1，再补 0。
    void Flush() {
        if (nbits_ == 0) return;
        acc_ |= (uint64_t(1) << (63 - nbits_));  // 终止 '1'
        ++nbits_;
        nbits_ = (nbits_ + 7) & ~7;  // 补齐到整字节（其余位为 0）
        while (nbits_ >= 8) {
            uint8_t byte = static_cast<uint8_t>(acc_ >> 56);
            acc_ <<= 8;
            nbits_ -= 8;
            out_.push_back(byte);
            if (byte == 0xFF) out_.push_back(0x00);  // 字节填充
        }
    }

private:
    std::vector<uint8_t>& out_;
    uint64_t acc_ = 0;
    int nbits_ = 0;
};

// ---------------------------------------------------------------------------
// 编码器主逻辑
// ---------------------------------------------------------------------------

// 执行 8x8 DCT + 量化，输出 zigzag 序的量化系数（block[0]=DC）。
void DctQuantBlock(const uint8_t plane[8][8], const uint8_t* qnat,
                   int16_t block[64]) {
    // 采样值减 128；s[x][y]，x 为水平频率维、y 为垂直频率维。
    int s[8][8];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) s[x][y] = static_cast<int>(plane[y][x]) - 128;

    // 水平方向：horiz[u][y] = sum_x s[x][y] * C(u)cos(...)
    int64_t horiz[8][8];
    for (int u = 0; u < 8; ++u) {
        for (int y = 0; y < 8; ++y) {
            int64_t sum = 0;
            for (int x = 0; x < 8; ++x)
                sum += static_cast<int64_t>(s[x][y]) * kCos[u][x];
            horiz[u][y] = sum;  // 量级 <= 8*255*8192 ~ 1.7e7
        }
    }

    // 垂直方向 + 缩放 + 量化，系数先按自然序存放。
    int16_t natural[64];
    for (int u = 0; u < 8; ++u) {
        for (int v = 0; v < 8; ++v) {
            int64_t sum = 0;
            for (int y = 0; y < 8; ++y) sum += horiz[u][y] * kCos[v][y];
            // DCT 缩放: 1/(8192^2 * 4)，即 >> 28，再按量化步长取整。
            const int64_t d = RShiftRound(sum, 28);
            // Baseline 8-bit 限制：DC/AC 量化电平限制在 11 位内，保证
            // DC 差分与 AC 幅度类别不会越过各自码表上限。
            const int level = std::clamp(QuantRound(d, qnat[v * 8 + u]), -1023, 1023);
            natural[v * 8 + u] = static_cast<int16_t>(level);
        }
    }

    // 按 zigzag 扫描顺序重排：kZigzag[j] = 扫描位置 j 的自然序索引。
    for (int j = 0; j < 64; ++j) block[j] = natural[kZigzag[j]];
}

// 记录 size 类别（正整数位数）。
inline int MagnitudeBits(int v) {
    if (v < 0) v = -v;
    int n = 0;
    while (v) {
        ++n;
        v >>= 1;
    }
    return n;
}

// JPEG 幅值位编码：正值写 v 的低 size 位；负值写 (v-1) 的低 size 位
// （使符号位总是 1=正、0=负，解码端按 -1 - 反码还原）。
inline uint32_t EncodeMag(int v, int size) {
    const uint32_t mask = (1u << size) - 1;
    if (v < 0) --v;  // 负数编码使用 v-1
    return static_cast<uint32_t>(v) & mask;
}

void EncodeBlock(int16_t block[64], int dc_pred[3], int comp, const HCode dcH[256],
                 const HCode acH[256], BitWriter& bw) {
    const int diff = static_cast<int>(block[0]) - dc_pred[comp];
    dc_pred[comp] = block[0];

    const int dcsize = MagnitudeBits(diff);
    bw.PutBits(dcH[dcsize].code, dcH[dcsize].len);
    if (dcsize > 0) bw.PutBits(EncodeMag(diff, dcsize), dcsize);

    int run = 0;
    for (int i = 1; i < 64; ++i) {
        const int v = block[i];
        if (v == 0) {
            ++run;
            continue;
        }
        while (run >= 16) {  // ZRL
            bw.PutBits(acH[0xF0].code, acH[0xF0].len);
            run -= 16;
        }
        const int size = MagnitudeBits(v);
        const int sym = (run << 4) | size;
        bw.PutBits(acH[sym].code, acH[sym].len);
        bw.PutBits(EncodeMag(v, size), size);
        run = 0;
    }
    if (run > 0) bw.PutBits(acH[0x00].code, acH[0x00].len);  // EOB
}

void PutMarker(std::vector<uint8_t>& out, uint8_t m1, uint8_t m2) {
    out.push_back(m1);
    out.push_back(m2);
}

void PutU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

}  // namespace

// ---------------------------------------------------------------------------
// 公开接口
// ---------------------------------------------------------------------------

std::vector<uint8_t> encode_jpeg_rgb24(const uint8_t* rgb, int width, int height,
                                       int row_stride) {
    if (rgb == nullptr || width <= 0 || height <= 0) return {};
    if (width > 65535 || height > 65535) return {};  // SOF0 字段为 16 位
    if (row_stride < 0) row_stride = width * 3;
    if (row_stride < width * 3) return {};

    // ---- 构造 16 对齐的整幅 padded 平面 ----
    const int mcu_w = (width + 15) / 16;
    const int mcu_h = (height + 15) / 16;
    const int pw = mcu_w * 16;
    const int ph = mcu_h * 16;

    std::vector<uint8_t> yp(static_cast<size_t>(pw) * ph);
    std::vector<uint8_t> cbp(static_cast<size_t>(pw) * ph);
    std::vector<uint8_t> crp(static_cast<size_t>(pw) * ph);

    const int src_bpp = 3;
    for (int py = 0; py < ph; ++py) {
        const int sy = std::min(py, height - 1);
        const uint8_t* srcrow = rgb + static_cast<size_t>(sy) * row_stride;
        uint8_t* yrow = &yp[static_cast<size_t>(py) * pw];
        uint8_t* cbrow = &cbp[static_cast<size_t>(py) * pw];
        uint8_t* crrow = &crp[static_cast<size_t>(py) * pw];
        for (int px = 0; px < pw; ++px) {
            const int sx = std::min(px, width - 1);
            const uint8_t* p = srcrow + static_cast<size_t>(sx) * src_bpp;
            RgbToYcc(p[0], p[1], p[2], yrow[px], cbrow[px], crrow[px]);
        }
    }

    // ---- 输出缓冲与静态 Huffman 码表 ----
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pw) * ph);  // 粗略上界估计
    BitWriter bw(out);

    // 熵编码中需要 Huffman 码表；静态构建一次。
    static const HCode* dc_lum = [] {
        static HCode t[256] = {};
        BuildHuff(kBitsDcLum, kValDc, 12, t);
        return t;
    }();
    static const HCode* ac_lum = [] {
        static HCode t[256] = {};
        BuildHuff(kBitsAcLum, kValAcLum, 162, t);
        return t;
    }();
    static const HCode* dc_chr = [] {
        static HCode t[256] = {};
        BuildHuff(kBitsDcChr, kValDc, 12, t);
        return t;
    }();
    static const HCode* ac_chr = [] {
        static HCode t[256] = {};
        BuildHuff(kBitsAcChr, kValAcChr, 162, t);
        return t;
    }();

    // ---- 段头 ----
    PutMarker(out, 0xFF, 0xD8);  // SOI

    // APP0 JFIF 1.01
    PutMarker(out, 0xFF, 0xE0);
    PutU16(out, 16);
    out.insert(out.end(), {'J', 'F', 'I', 'F', 0x00, 0x01, 0x01, 0x00,
                           0x00, 0x01, 0x00, 0x01, 0x00, 0x00});

    // DQT x2（量化表以 zigzag 序存储，每段一张表）
    PutMarker(out, 0xFF, 0xDB);
    PutU16(out, 2 + 1 + 64);
    out.push_back(0x00);  // Pq=0, Tq=0 (亮度)
    for (int i = 0; i < 64; ++i) out.push_back(kQuantLuma[kZigzag[i]]);
    PutMarker(out, 0xFF, 0xDB);
    PutU16(out, 2 + 1 + 64);
    out.push_back(0x01);  // Pq=0, Tq=1 (色度)
    for (int i = 0; i < 64; ++i) out.push_back(kQuantChroma[kZigzag[i]]);

    // DHT：DC0(亮度), AC0(亮度), DC1(色度), AC1(色度)
    const struct {
        uint8_t id;
        const uint8_t* bits;
        const uint8_t* val;
        int n;
    } huff_tabs[4] = {
        {0x00, kBitsDcLum, kValDc, 12},
        {0x10, kBitsAcLum, kValAcLum, 162},
        {0x01, kBitsDcChr, kValDc, 12},
        {0x11, kBitsAcChr, kValAcChr, 162},
    };
    for (const auto& ht : huff_tabs) {
        PutMarker(out, 0xFF, 0xC4);
        PutU16(out, static_cast<uint16_t>(2 + 1 + 16 + ht.n));
        out.push_back(ht.id);
        for (int i = 1; i <= 16; ++i) out.push_back(ht.bits[i]);
        for (int i = 0; i < ht.n; ++i) out.push_back(ht.val[i]);
    }

    // SOF0：baseline，8 位，YCbCr 4:2:0
    PutMarker(out, 0xFF, 0xC0);
    PutU16(out, 17);
    out.push_back(8);
    PutU16(out, static_cast<uint16_t>(height));
    PutU16(out, static_cast<uint16_t>(width));
    out.push_back(3);
    out.push_back(1);  // 分量 1 = Y
    out.push_back(0x22);
    out.push_back(0);
    out.push_back(2);  // 分量 2 = Cb
    out.push_back(0x11);
    out.push_back(1);
    out.push_back(3);  // 分量 3 = Cr
    out.push_back(0x11);
    out.push_back(1);

    // SOS
    PutMarker(out, 0xFF, 0xDA);
    PutU16(out, 12);
    out.push_back(3);
    out.push_back(1);  // Y: DC 表 0 / AC 表 0
    out.push_back(0x00);
    out.push_back(2);
    out.push_back(0x11);  // Cb: DC 表 1 / AC 表 1
    out.push_back(3);
    out.push_back(0x11);  // Cr: DC 表 1 / AC 表 1
    out.push_back(0);  // Ss
    out.push_back(63);  // Se
    out.push_back(0);  // Ah/Al

    // ---- 熵编码 ----
    // 16x16 MCU 行主序遍历；每 MCU 编码 Y0,Y1,Y2,Y3,Cb,Cr 六个块。
    int dc_pred[3] = {0, 0, 0};
    uint8_t block_px[8][8];
    int16_t block[64];

    const int mbw = mcu_w * 2;  // 8x8 块在宽方向的个数
    const int mbh = mcu_h * 2;
    for (int by = 0; by < mbh; by += 2) {
        for (int bx = 0; bx < mbw; bx += 2) {
            // 四个亮度块
            for (int sub = 0; sub < 4; ++sub) {
                const int ox = (sub & 1) * 8;
                const int oy = (sub >> 1) * 8;
                const int bx8 = bx + ox / 8;
                const int by8 = by + oy / 8;
                const int px0 = bx8 * 8;
                const int py0 = by8 * 8;
                for (int yy = 0; yy < 8; ++yy)
                    for (int xx = 0; xx < 8; ++xx)
                        block_px[yy][xx] = yp[static_cast<size_t>(py0 + yy) * pw + px0 + xx];
                DctQuantBlock(block_px, kQuantLuma, block);
                EncodeBlock(block, dc_pred, 0, dc_lum, ac_lum, bw);
            }
            // 色度块（4:2:0 2x2 平均）。色度平面为全分辨率 padded，
            // 每 MCU 覆盖的色度 8x8 块对应全分辨率像素起点 = MCU 原点。
            const int cpx0 = bx * 8;  // MCU 列号 = bx/2 -> 像素原点 (bx/2)*16 = bx*8
            const int cpy0 = by * 8;
            for (int comp = 1; comp <= 2; ++comp) {
                const uint8_t* plane = (comp == 1) ? cbp.data() : crp.data();
                // 每个色度采样为该处 2x2 全分辨率像素的均值。
                for (int cyy = 0; cyy < 8; ++cyy) {
                    for (int cxx = 0; cxx < 8; ++cxx) {
                        const int px = cpx0 + cxx * 2;
                        const int py = cpy0 + cyy * 2;
                        const uint8_t a = plane[static_cast<size_t>(py) * pw + px];
                        const uint8_t b = plane[static_cast<size_t>(py) * pw + px + 1];
                        const uint8_t c = plane[static_cast<size_t>(py + 1) * pw + px];
                        const uint8_t d = plane[static_cast<size_t>(py + 1) * pw + px + 1];
                        block_px[cyy][cxx] = static_cast<uint8_t>((a + b + c + d + 2) >> 2);
                    }
                }
                DctQuantBlock(block_px, kQuantChroma, block);
                EncodeBlock(block, dc_pred, comp, dc_chr, ac_chr, bw);
            }
        }
    }

    bw.Flush();
    PutMarker(out, 0xFF, 0xD9);  // EOI
    return out;
}

}  // namespace platform
}  // namespace sm
