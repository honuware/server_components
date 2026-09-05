#include "image_resize.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <boost/gil.hpp>
#include <boost/gil/extension/io/jpeg.hpp>
#include <boost/gil/extension/io/png.hpp>
#include <boost/gil/extension/io/tiff.hpp>
#include <boost/gil/extension/numeric/sampler.hpp>
#include <boost/gil/extension/numeric/resample.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>

#include <sstream>
#include <string>

namespace ImageResize {
    namespace
    {
        using VectorSink = boost::iostreams
            ::back_insert_device<std::vector<char>>;
        using VectorStream = boost::iostreams::stream<VectorSink>;
        // Input needs to be an array even if it is coming from a vector.
        using ArraySource = boost::iostreams::array_source;
        using ArrayStream = boost::iostreams::stream<ArraySource>;

        using ::testing::Contains;
        using ::testing::ElementsAreArray;
        using ::testing::UnorderedElementsAre;
        using ::testing::ElementsAre;
        using ::testing::Eq;

        void MakeRedGreenImage(boost::gil::rgb8_image_t& img) {
            auto imgView = boost::gil::view(img);
            boost::gil::pixel<uint8_t, boost::gil::rgb_layout_t> pix;
            boost::gil::rgb8_pixel_t red(255, 0, 0);
            boost::gil::rgb8_pixel_t green(0, 255, 0);
            int halfWidth = imgView.width() / 2;
            for (int y = 0; y < imgView.height(); ++y) {
                auto iter = imgView.row_begin(y);
                for (int x = 0; x < imgView.width(); ++x) {
                    if (x < halfWidth) {
                        iter[x] = red;
                    }
                    else {
                        iter[x] = green;
                    }
                }
            }
        }

        std::vector<char> ImageToArray(const boost::gil::rgb8_image_t& img) {
            std::vector<char> result;
            VectorSink sink(result);
            VectorStream imgStream(sink);
            boost::gil::write_view(
                imgStream, 
                boost::gil::const_view(img), 
                boost::gil::jpeg_tag());
            return result;
        }

        boost::gil::rgb8_image_t ArrayToImage(const std::vector<char>& vec) {
            boost::gil::rgb8_image_t result;
            ArraySource source(vec.data(), vec.size());
            ArrayStream arrayStream(source);
            boost::gil::read_image(arrayStream, result, boost::gil::jpeg_tag());
            return result;
        }

        // Bilinear resampling frequently makes these a little off
        bool FuzzyMatch(int n1, int n2) {
            int diff = n1 - n2;
            return 5 > diff && diff > -5;
        }

        template <class Pixel>
        bool EqualPixel(const Pixel& pixel1, const Pixel& pixel2) {
            if (!FuzzyMatch(pixel1[0], pixel2[0]) || !FuzzyMatch(pixel1[1], pixel2[1]) || !FuzzyMatch(pixel1[2], pixel2[2])) {
                std::cout << "Mismatched pixels! ("
                    << (int)pixel1[0] << ", " << (int)pixel1[1] << ", " << (int)pixel1[2] << ") ("
                    << (int)pixel2[0] << ", " << (int)pixel2[1] << ", " << (int)pixel2[2] << ")\n";
                return false;
            }
            return true;
        }

        // Anything near the middle gets mangled by bilinear filtering so check the corners
        // and still do fuzzy matching.
        bool CheckCorners(
            const boost::gil::rgb8_image_t& img1,
            const boost::gil::rgb8_image_t& img2) {
            auto view1 = boost::gil::const_view(img1);
            auto view2 = boost::gil::const_view(img2);
            if (view1.width() != view2.width()
                || view1.height() != view2.height()) {
                return false;
            }
            return
                EqualPixel(view1(0, 0), view2(0, 0)) &&
                EqualPixel(view1(0, view1.height() - 1), view2(0, view1.height() - 1)) &&
                EqualPixel(view1(view1.width() - 1, 0), view2(view1.width() - 1, 0)) &&
                EqualPixel(view1(view1.width() - 1, view1.height() - 1), view2(view1.width() - 1, view1.height() - 1));
        }

        TEST(ImageResizeTest, ResizeImageBasic)
        {
            boost::gil::rgb8_image_t imgBig(64, 64), imgSmall(32, 32);
            MakeRedGreenImage(imgBig);
            MakeRedGreenImage(imgSmall);
            auto vecBig = ImageToArray(imgBig);
            auto vecSmall = ResizeImage(vecBig, 32, 32, IMAGE_TYPE_JPEG);
            ASSERT_TRUE(CheckCorners(ArrayToImage(vecSmall), imgSmall));
        }

        TEST(ImageResizeTest, GetImageDimensionsBasic)
        {
            boost::gil::rgb8_image_t img(128, 96);
            MakeRedGreenImage(img);
            auto vec = ImageToArray(img);
            auto dims = GetImageDimensions(vec, IMAGE_TYPE_JPEG);
            EXPECT_EQ(dims.width, 128);
            EXPECT_EQ(dims.height, 96);
        }

        // ---- PNG transparency ----

        // Left half opaque red, right half FULLY TRANSPARENT. The transparent
        // pixels carry rgb 0,0,0 — exactly how a real exported PNG stores
        // them, and exactly what surfaces as black if the alpha is dropped.
        void MakeHalfTransparentImage(boost::gil::rgba8_image_t& img) {
            auto imgView = boost::gil::view(img);
            boost::gil::rgba8_pixel_t opaqueRed(255, 0, 0, 255);
            boost::gil::rgba8_pixel_t transparent(0, 0, 0, 0);
            int halfWidth = imgView.width() / 2;
            for (int y = 0; y < imgView.height(); ++y) {
                auto iter = imgView.row_begin(y);
                for (int x = 0; x < imgView.width(); ++x) {
                    iter[x] = (x < halfWidth) ? opaqueRed : transparent;
                }
            }
        }

        std::vector<char> PngToArray(const boost::gil::rgba8_image_t& img) {
            std::vector<char> result;
            VectorSink sink(result);
            VectorStream imgStream(sink);
            boost::gil::write_view(
                imgStream, boost::gil::const_view(img), boost::gil::png_tag());
            imgStream.flush();   // see OpaquePngToArray
            return result;
        }

        boost::gil::rgba8_image_t ArrayToPng(const std::vector<char>& vec) {
            boost::gil::rgba8_image_t result;
            ArraySource source(vec.data(), vec.size());
            ArrayStream arrayStream(source);
            boost::gil::read_image(arrayStream, result, boost::gil::png_tag());
            return result;
        }

        // A resized PNG must still be transparent where it was transparent.
        // Reading an RGBA source into an rgb8 image silently drops the alpha
        // and every see-through pixel comes back BLACK — which is how the
        // membership tier icons rendered as black squares (Polish Phase 5.4).
        TEST(ImageResizeTest, ResizePngKeepsItsAlphaChannel)
        {
            boost::gil::rgba8_image_t img(64, 64);
            MakeHalfTransparentImage(img);

            auto resized = ResizeImage(PngToArray(img), 32, 32, IMAGE_TYPE_PNG);
            auto out = ArrayToPng(resized);
            auto view = boost::gil::const_view(out);
            ASSERT_EQ(view.width(), 32);
            ASSERT_EQ(view.height(), 32);

            // Corners, so bilinear blending across the middle seam is not what
            // is being measured.
            const auto opaqueCorner = view(0, 0);
            EXPECT_EQ(static_cast<int>(opaqueCorner[3]), 255)
                << "the opaque half lost its opacity";
            EXPECT_TRUE(FuzzyMatch(opaqueCorner[0], 255));

            const auto transparentCorner = view(view.width() - 1, 0);
            EXPECT_EQ(static_cast<int>(transparentCorner[3]), 0)
                << "the transparent half came back opaque — alpha was dropped, "
                   "which renders as a black background";
        }

        std::vector<char> OpaquePngToArray(const boost::gil::rgb8_image_t& img) {
            std::vector<char> result;
            VectorSink sink(result);
            VectorStream imgStream(sink);
            boost::gil::write_view(
                imgStream, boost::gil::const_view(img), boost::gil::png_tag());
            // Explicit: the stream buffers, and without this the tail of the
            // PNG only reaches `result` when the stream is destroyed — i.e.
            // after it has already been returned and read. That produces a
            // truncated file and `png_check_validity: ... iostream error`.
            imgStream.flush();
            return result;
        }

        // The other half of the contract, and it is NOT a formality: an opaque
        // RGB PNG must keep taking the rgb8 path. Forcing one through rgba8
        // returns garbage — a wrong alpha and shifted colour — so "always use
        // RGBA for PNG" trades the black-background bug for a broken-colour
        // one. This test is what caught that.
        TEST(ImageResizeTest, ResizeOpaquePngStaysOnTheRgbPath)
        {
            boost::gil::rgb8_image_t img(64, 64);
            MakeRedGreenImage(img);

            auto resized = ResizeImage(
                OpaquePngToArray(img), 32, 32, IMAGE_TYPE_PNG);

            // Reads back as RGB, which is the point: an opaque PNG comes out
            // opaque, not silently widened to a 4-channel image.
            boost::gil::rgb8_image_t out;
            ArraySource source(resized.data(), resized.size());
            ArrayStream arrayStream(source);
            boost::gil::read_image(arrayStream, out, boost::gil::png_tag());

            auto view = boost::gil::const_view(out);
            ASSERT_EQ(view.width(), 32);
            ASSERT_EQ(view.height(), 32);
            // Left half still red, right half still green.
            EXPECT_TRUE(EqualPixel(
                view(0, 0), boost::gil::rgb8_pixel_t(255, 0, 0)));
            EXPECT_TRUE(EqualPixel(
                view(view.width() - 1, 0), boost::gil::rgb8_pixel_t(0, 255, 0)));
        }

        // The routing decision itself, stated directly: alpha in the IHDR
        // colour type is what selects the RGBA path, not the file extension.
        TEST(ImageResizeTest, PngColourTypeDecidesWhichPathAResizeTakes)
        {
            boost::gil::rgba8_image_t transparent(16, 16);
            MakeHalfTransparentImage(transparent);
            const auto rgbaBytes = PngToArray(transparent);

            boost::gil::rgb8_image_t opaque(16, 16);
            MakeRedGreenImage(opaque);
            const auto rgbBytes = OpaquePngToArray(opaque);

            // IHDR colour type is byte 25; bit 2 is the alpha flag.
            constexpr size_t kColourTypeOffset = 25;
            EXPECT_EQ(
                static_cast<unsigned char>(rgbaBytes[kColourTypeOffset]) & 4, 4)
                << "fixture is not actually a transparent PNG";
            EXPECT_EQ(
                static_cast<unsigned char>(rgbBytes[kColourTypeOffset]) & 4, 0)
                << "fixture is not actually an opaque PNG";

            // And each survives its own path — the pair of assertions the two
            // tests above make separately, tied to the routing input here.
            EXPECT_EQ(static_cast<int>(boost::gil::const_view(
                ArrayToPng(ResizeImage(rgbaBytes, 8, 8, IMAGE_TYPE_PNG)))(
                    7, 0)[3]), 0);
            EXPECT_NO_THROW(ResizeImage(rgbBytes, 8, 8, IMAGE_TYPE_PNG));
        }

        // ---- TIFF ----
        //
        // IMAGE_TYPE_TIFF routes to libtiff through boost::gil's tiff_tag, and
        // before the VS2026 migration NOTHING exercised it: the enum value, the
        // ${TIFF_LIB} link edge and both arms of the production switch existed
        // with no test behind them, while JPEG and PNG were covered thoroughly.
        //
        // That mattered because the migration forces libtiff 4.6.0 -> 4.7.2 --
        // 4.6.0's recipe tool_requires cmake/[>=3.18 <4], and a CMake 3.x cannot
        // emit the "Visual Studio 18 2026" generator, so 4.6.0 simply cannot build
        // on VS2026. A decoder change arriving with that bump would otherwise have
        // been silent. These two mirror the JPEG cases exactly, so a TIFF-specific
        // regression shows up as a TIFF-only failure.

        // A TIFF fixture CANNOT be built with the VectorSink/back_insert_device
        // pattern the JPEG and PNG helpers use. A TIFF writer has to seek back and
        // patch the IFD offset into the header once it knows it, and
        // back_insert_device is append-only, so boost::gil rejects the device
        // outright with "no random access: iostream error". std::ostringstream is
        // seekable, so the fixture goes through one of those. (Reading is fine on
        // the existing ArraySource -- array devices ARE seekable, which is why
        // GetImageDimensionsTiff below passes on the production read path.)
        std::vector<char> TiffToArray(const boost::gil::rgb8_image_t& img) {
            std::ostringstream out(std::ios_base::out | std::ios_base::binary);
            boost::gil::write_view(
                out, boost::gil::const_view(img), boost::gil::tiff_tag());
            const std::string bytes = out.str();
            return std::vector<char>(bytes.begin(), bytes.end());
        }

        // The READ path, which is the half the libtiff bump most directly affects:
        // decoding a real TIFF through the production entry point.
        TEST(ImageResizeTest, GetImageDimensionsTiff)
        {
            boost::gil::rgb8_image_t img(128, 96);
            MakeRedGreenImage(img);
            auto vec = TiffToArray(img);
            auto dims = GetImageDimensions(vec, IMAGE_TYPE_TIFF);
            EXPECT_EQ(dims.width, 128);
            EXPECT_EQ(dims.height, 96);
        }

        // THIS PINS A PRE-EXISTING DEFECT. It is NOT the behaviour we want.
        //
        // ResizeImage writes its output through VectorSink (back_insert_device),
        // which is append-only. JPEG and PNG stream strictly forwards and do not
        // care. A TIFF writer must seek back to patch the IFD offset, so the
        // IMAGE_TYPE_TIFF branch of WriteView throws "no random access: iostream
        // error" for EVERY input -- resizing a TIFF has never worked, in any
        // build, on any platform. Nothing caught it because until now nothing
        // tested TIFF at all; the enum value, the ${TIFF_LIB} link edge and both
        // switch arms all existed with no coverage behind them.
        //
        // Deliberately asserted rather than fixed here: the fix changes production
        // image behaviour and deserves its own commit and review, not a smuggled
        // ride inside a dependency-version bump.
        //
        // WHEN THAT FIX LANDS, THIS TEST SHOULD FAIL. That is the point of it.
        // Replace it with the round-trip assertion ResizeImageBasic uses for JPEG.
        TEST(ImageResizeTest, ResizeTiffThrowsBecauseTheOutputSinkCannotSeek)
        {
            boost::gil::rgb8_image_t img(64, 64);
            MakeRedGreenImage(img);
            const auto tiffBytes = TiffToArray(img);
            ASSERT_FALSE(tiffBytes.empty())
                << "fixture is not a real TIFF -- the helper itself is broken";
            EXPECT_THROW(
                ResizeImage(tiffBytes, 32, 32, IMAGE_TYPE_TIFF), std::exception);
        }

    } // namespace {
}  // namespace ImageResize
