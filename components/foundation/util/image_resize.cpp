#include "image_resize.h"

#include <boost/gil.hpp>
#include <boost/gil/extension/io/bmp.hpp>
#include <boost/gil/extension/io/png.hpp>
#include <boost/gil/extension/io/tiff.hpp>
#include <boost/gil/extension/io/jpeg.hpp>
#include <boost/gil/extension/numeric/sampler.hpp>
#include <boost/gil/extension/numeric/resample.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>

namespace ImageResize {

    namespace {
        using VectorSink = boost::iostreams::
            back_insert_device<std::vector<char>>;
        using VectorStream = boost::iostreams::stream<VectorSink>;
        // Input needs to be an array even if it is coming from a vector.
        using ArraySource = boost::iostreams::array_source;
        using ArrayStream = boost::iostreams::stream<ArraySource>;

        template <typename Device, typename Image>
        void ReadImage(Device& device, Image& image, ImageType imageType) {
            // Use read_and_convert_image instead of read_image so that
            // images with different color spaces (e.g. RGBA or grayscale
            // PNGs) are automatically converted to the destination type.
            switch (imageType) {
            case IMAGE_TYPE_BMP:
                boost::gil::read_and_convert_image(device, image, boost::gil::bmp_tag());
                break;
            case IMAGE_TYPE_JPEG:
                boost::gil::read_and_convert_image(device, image, boost::gil::jpeg_tag());
                break;
            case IMAGE_TYPE_PNG:
                boost::gil::read_and_convert_image(device, image, boost::gil::png_tag());
                break;
            case IMAGE_TYPE_TIFF:
                boost::gil::read_and_convert_image(device, image, boost::gil::tiff_tag());
                break;
            }
        }

        template <typename Device, typename View>
        void WriteView(Device& device, View& view, ImageType imageType) {
            switch (imageType) {
            case IMAGE_TYPE_BMP:
                boost::gil::write_view(device, view, boost::gil::bmp_tag());
                break;
            case IMAGE_TYPE_JPEG:
                boost::gil::write_view(device, view, boost::gil::jpeg_tag());
                break;
            case IMAGE_TYPE_PNG:
                boost::gil::write_view(device, view, boost::gil::png_tag());
                break;
            case IMAGE_TYPE_TIFF:
                boost::gil::write_view(device, view, boost::gil::tiff_tag());
                break;
            }
        }

    }

    ImageDimensions GetImageDimensions(
        const std::vector<char>& srcImage, ImageType imageType) {
        boost::gil::rgb8_image_t img;
        ArraySource source(srcImage.data(), srcImage.size());
        ArrayStream inputStream(source);
        ReadImage(inputStream, img, imageType);
        return { static_cast<int>(img.width()),
                 static_cast<int>(img.height()) };
    }

    std::vector<char> ResizeImage(
        const std::vector<char>& srcImage, int width, int height, ImageType imageType) {
        std::vector<char> result;
        boost::gil::rgb8_image_t imgSrc;
        ArraySource sink(srcImage.data(), srcImage.size());
        ArrayStream inputStream(sink);
        ReadImage(inputStream, imgSrc, imageType);
        boost::gil::rgb8_image_t imgDest(width, height);
        boost::gil::resize_view(
            boost::gil::const_view(imgSrc),
            boost::gil::view(imgDest),
            boost::gil::bilinear_sampler{});
        VectorSink outputSink(result);
        VectorStream outputStream(outputSink);
        WriteView(outputStream, boost::gil::const_view(imgDest), imageType);
        return result;
    }

}  // namespace ImageResize
