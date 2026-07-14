#include "image_resize.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <boost/gil.hpp>
#include <boost/gil/extension/io/jpeg.hpp>
#include <boost/gil/extension/numeric/sampler.hpp>
#include <boost/gil/extension/numeric/resample.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>

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

    } // namespace {
}  // namespace ImageResize
