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

        // Does this PNG actually carry transparency?
        //
        // Asked of the FILE rather than assumed, because the two answers need
        // different pixel types and neither is safe for the other: an RGBA
        // source read as rgb8 loses its alpha (transparency turns black),
        // while an RGB source read as rgba8 comes back as garbage — a wrong
        // alpha and shifted colour, which is what the opaque-PNG guard test
        // caught. Only genuinely transparent PNGs take the RGBA path; every
        // other PNG keeps the long-standing RGB one.
        //
        // The colour type is IHDR byte 25 (libpng's own numbering): bit 2 is
        // the alpha flag, so 4 = gray+alpha and 6 = RGBA. Read straight from
        // the header — the file is already in memory and this avoids decoding
        // the image twice just to ask one question.
        //
        // Palette PNGs (3) with a tRNS chunk are transparent too, but they
        // have always gone down the RGB path and this change deliberately does
        // not alter what they do.
        bool PngHasAlphaChannel(const std::vector<char>& srcImage) {
            constexpr size_t kColourTypeOffset = 25;
            constexpr unsigned char kAlphaFlag = 4;
            if (srcImage.size() <= kColourTypeOffset) {
                return false;
            }
            const auto colourType =
                static_cast<unsigned char>(srcImage[kColourTypeOffset]);
            return (colourType & kAlphaFlag) != 0;
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
        // A TRANSPARENT PNG keeps its alpha channel; everything else stays RGB.
        //
        // PNG is the format people use precisely BECAUSE it has transparency —
        // logos, icons, artwork meant to sit on any background. Resizing one
        // through an `rgb8_image_t` threw the alpha away (read_and_convert does
        // that conversion silently), and a fully transparent pixel is stored as
        // 0,0,0,0 — so the transparency came back BLACK. That is how the
        // membership tier icons rendered as black squares.
        //
        // Note the condition is "this PNG HAS alpha", not "this is a PNG":
        // forcing an opaque RGB PNG through the rgba8 path returns garbage (a
        // wrong alpha and shifted colour). Both directions of that conversion
        // are lossy, so ask the file — see PngHasAlphaChannel.
        //
        // This path calls png_tag directly instead of going through the
        // ReadImage/WriteView switches below, and that is LOAD-BEARING: a
        // switch instantiates all four format branches for whatever pixel type
        // it is given, and `write_view` with `jpeg_tag` does not accept RGBA —
        // the enable_if fails to compile even though only the PNG branch could
        // ever run.
        //
        // Caveat worth knowing: this resamples straight (non-premultiplied)
        // RGBA, so a hard edge against fully transparent pixels can pick up a
        // slight dark fringe. Far smaller than the solid black rectangle it
        // replaces; premultiplying around the resize is the fix if it shows.
        if (imageType == IMAGE_TYPE_PNG && PngHasAlphaChannel(srcImage)) {
            std::vector<char> result;
            boost::gil::rgba8_image_t imgSrc;
            ArraySource pngSource(srcImage.data(), srcImage.size());
            ArrayStream pngInput(pngSource);
            boost::gil::read_and_convert_image(
                pngInput, imgSrc, boost::gil::png_tag());
            boost::gil::rgba8_image_t imgDest(width, height);
            boost::gil::resize_view(
                boost::gil::const_view(imgSrc),
                boost::gil::view(imgDest),
                boost::gil::bilinear_sampler{});
            VectorSink pngSink(result);
            VectorStream pngOutput(pngSink);
            boost::gil::write_view(
                pngOutput, boost::gil::const_view(imgDest),
                boost::gil::png_tag());
            // Explicit, because the stream buffers: without it the tail of the
            // image only reaches `result` when the stream is destroyed, which
            // is after this returns. It happens to work through NRVO — the
            // local and the returned vector are the same object — but relying
            // on that is how a caller ends up with a truncated file.
            pngOutput.flush();
            return result;
        }

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
        outputStream.flush();   // see the PNG branch above
        return result;
    }

}  // namespace ImageResize
