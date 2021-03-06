/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Benchmark.h"

#include "Resources.h"
#include "SkAutoPixmapStorage.h"
#include "SkData.h"
#include "SkFloatToDecimal.h"
#include "SkGradientShader.h"
#include "SkImage.h"
#include "SkPixmap.h"
#include "SkRandom.h"
#include "SkStream.h"
#include "SkTo.h"

namespace {
struct WStreamWriteTextBenchmark : public Benchmark {
    std::unique_ptr<SkWStream> fWStream;
    WStreamWriteTextBenchmark() : fWStream(new SkNullWStream) {}
    const char* onGetName() override { return "WStreamWriteText"; }
    bool isSuitableFor(Backend backend) override {
        return backend == kNonRendering_Backend;
    }
    void onDraw(int loops, SkCanvas*) override {
        while (loops-- > 0) {
            for (int i = 1000; i-- > 0;) {
                fWStream->writeText("HELLO SKIA!\n");
            }
        }
    }
};
}  // namespace

DEF_BENCH(return new WStreamWriteTextBenchmark;)

// Test speed of SkFloatToDecimal for typical floats that
// might be found in a PDF document.
struct PDFScalarBench : public Benchmark {
    PDFScalarBench(const char* n, float (*f)(SkRandom*)) : fName(n), fNextFloat(f) {}
    const char* fName;
    float (*fNextFloat)(SkRandom*);
    bool isSuitableFor(Backend b) override {
        return b == kNonRendering_Backend;
    }
    const char* onGetName() override { return fName; }
    void onDraw(int loops, SkCanvas*) override {
        SkRandom random;
        char dst[kMaximumSkFloatToDecimalLength];
        while (loops-- > 0) {
            auto f = fNextFloat(&random);
            (void)SkFloatToDecimal(f, dst);
        }
    }
};

float next_common(SkRandom* random) {
    return random->nextRangeF(-500.0f, 1500.0f);
}
float next_any(SkRandom* random) {
    union { uint32_t u; float f; };
    u = random->nextU();
    static_assert(sizeof(float) == sizeof(uint32_t), "");
    return f;
}

DEF_BENCH(return new PDFScalarBench("PDFScalar_common", next_common);)
DEF_BENCH(return new PDFScalarBench("PDFScalar_random", next_any);)

#ifdef SK_SUPPORT_PDF

#include "SkPDFBitmap.h"
#include "SkPDFDocumentPriv.h"
#include "SkPDFShader.h"
#include "SkPDFUtils.h"

namespace {
static void test_pdf_object_serialization(const sk_sp<SkPDFObject> object) {
    // SkDebugWStream wStream;
    SkNullWStream wStream;
    SkPDFObjNumMap objNumMap;
    objNumMap.addObjectRecursively(object.get());
    for (size_t i = 0; i < objNumMap.objects().size(); ++i) {
        SkPDFObject* object = objNumMap.objects()[i].get();
        wStream.writeDecAsText(i + 1);
        wStream.writeText(" 0 obj\n");
        object->emitObject(&wStream);
        wStream.writeText("\nendobj\n");
    }
}

class PDFImageBench : public Benchmark {
public:
    PDFImageBench() {}
    ~PDFImageBench() override {}

protected:
    const char* onGetName() override { return "PDFImage"; }
    bool isSuitableFor(Backend backend) override {
        return backend == kNonRendering_Backend;
    }
    void onDelayedSetup() override {
        sk_sp<SkImage> img(GetResourceAsImage("images/color_wheel.png"));
        if (img) {
            // force decoding, throw away reference to encoded data.
            SkAutoPixmapStorage pixmap;
            pixmap.alloc(SkImageInfo::MakeN32Premul(img->dimensions()));
            if (img->readPixels(pixmap, 0, 0)) {
                fImage = SkImage::MakeRasterCopy(pixmap);
            }
        }
    }
    void onDraw(int loops, SkCanvas*) override {
        if (!fImage) {
            return;
        }
        while (loops-- > 0) {
            auto object = SkPDFCreateBitmapObject(fImage);
            SkASSERT(object);
            if (!object) {
                return;
            }
            test_pdf_object_serialization(object);
        }
    }

private:
    sk_sp<SkImage> fImage;
};

class PDFJpegImageBench : public Benchmark {
public:
    PDFJpegImageBench() {}
    ~PDFJpegImageBench() override {}

protected:
    const char* onGetName() override { return "PDFJpegImage"; }
    bool isSuitableFor(Backend backend) override {
        return backend == kNonRendering_Backend;
    }
    void onDelayedSetup() override {
        sk_sp<SkImage> img(GetResourceAsImage("images/mandrill_512_q075.jpg"));
        if (!img) { return; }
        sk_sp<SkData> encoded = img->refEncodedData();
        SkASSERT(encoded);
        if (!encoded) { return; }
        fImage = img;
    }
    void onDraw(int loops, SkCanvas*) override {
        if (!fImage) {
            SkDEBUGFAIL("");
            return;
        }
        while (loops-- > 0) {
            auto object = SkPDFCreateBitmapObject(fImage);
            SkASSERT(object);
            if (!object) {
                return;
            }
            test_pdf_object_serialization(object);
        }
    }

private:
    sk_sp<SkImage> fImage;
};

/** Test calling DEFLATE on a 78k PDF command stream. Used for measuring
    alternate zlib settings, usage, and library versions. */
class PDFCompressionBench : public Benchmark {
public:
    PDFCompressionBench() {}
    ~PDFCompressionBench() override {}

protected:
    const char* onGetName() override { return "PDFCompression"; }
    bool isSuitableFor(Backend backend) override {
        return backend == kNonRendering_Backend;
    }
    void onDelayedSetup() override {
        fAsset = GetResourceAsStream("pdf_command_stream.txt");
    }
    void onDraw(int loops, SkCanvas*) override {
        SkASSERT(fAsset);
        if (!fAsset) { return; }
        while (loops-- > 0) {
            sk_sp<SkPDFObject> object =
                sk_make_sp<SkPDFSharedStream>(
                        std::unique_ptr<SkStreamAsset>(fAsset->duplicate()));
            test_pdf_object_serialization(object);
        }
    }

private:
    std::unique_ptr<SkStreamAsset> fAsset;
};

struct PDFColorComponentBench : public Benchmark {
    bool isSuitableFor(Backend b) override {
        return b == kNonRendering_Backend;
    }
    const char* onGetName() override { return "PDFColorComponent"; }
    void onDraw(int loops, SkCanvas*) override {
        char dst[5];
        while (loops-- > 0) {
            for (int i = 0; i < 256; ++i) {
                (void)SkPDFUtils::ColorToDecimal(SkToU8(i), dst);
            }
        }
    }
};

struct PDFShaderBench : public Benchmark {
    sk_sp<SkShader> fShader;
    const char* onGetName() final { return "PDFShader"; }
    bool isSuitableFor(Backend b) final { return b == kNonRendering_Backend; }
    void onDelayedSetup() final {
        const SkPoint pts[2] = {{0.0f, 0.0f}, {100.0f, 100.0f}};
        const SkColor colors[] = {
            SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE,
            SK_ColorWHITE, SK_ColorBLACK,
        };
        fShader = SkGradientShader::MakeLinear(
                pts, colors, nullptr, SK_ARRAY_COUNT(colors),
                SkShader::kClamp_TileMode);
    }
    void onDraw(int loops, SkCanvas*) final {
        SkASSERT(fShader);
        while (loops-- > 0) {
            SkNullWStream nullStream;
            SkPDFDocument doc(&nullStream, SkPDF::Metadata());
            sk_sp<SkPDFObject> shader = SkPDFMakeShader(&doc, fShader.get(), SkMatrix::I(),
                                                        {0, 0, 400, 400}, SK_ColorBLACK);
        }
    }
};

struct WritePDFTextBenchmark : public Benchmark {
    std::unique_ptr<SkWStream> fWStream;
    WritePDFTextBenchmark() : fWStream(new SkNullWStream) {}
    const char* onGetName() override { return "WritePDFText"; }
    bool isSuitableFor(Backend backend) override {
        return backend == kNonRendering_Backend;
    }
    void onDraw(int loops, SkCanvas*) override {
        static const char kHello[] = "HELLO SKIA!\n";
        static const char kBinary[] = "\001\002\003\004\005\006";
        while (loops-- > 0) {
            for (int i = 1000; i-- > 0;) {
                SkPDFWriteString(fWStream.get(), kHello, strlen(kHello));
                SkPDFWriteString(fWStream.get(), kBinary, strlen(kBinary));
            }
        }
    }
};

}  // namespace
DEF_BENCH(return new PDFImageBench;)
DEF_BENCH(return new PDFJpegImageBench;)
DEF_BENCH(return new PDFCompressionBench;)
DEF_BENCH(return new PDFColorComponentBench;)
DEF_BENCH(return new PDFShaderBench;)
DEF_BENCH(return new WritePDFTextBenchmark;)

#endif

