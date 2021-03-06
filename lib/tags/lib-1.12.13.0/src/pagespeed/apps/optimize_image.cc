// Copyright 2009 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Command line utility to optimize images

#include <stdio.h>

#include <algorithm>
#include <fstream>
#include <iostream>  // for std::cin and std::cout
#include <iterator>
#include <string>

#include "pagespeed/image_compression/gif_reader.h"
#include "pagespeed/image_compression/image_converter.h"
#include "pagespeed/image_compression/jpeg_optimizer.h"
#include "pagespeed/image_compression/png_optimizer.h"
#include "third_party/gflags/src/google/gflags.h"

DEFINE_string(input_file, "", "Path to input file. '-' to read from stdin.");
DEFINE_string(output_file, "", "Path to output file.");
DEFINE_bool(jpeg_lossy, false,
            "If true, lossy JPEG compression will be performed.");
DEFINE_int32(jpeg_quality, 85,
             "JPEG quality (0-100). Only applies if --jpeg_lossy is set.");
DEFINE_bool(jpeg_progressive, false,
            "If true, will create a progressive JPEG.");
DEFINE_int32(jpeg_num_scans, -1,
             "Number of progressive scans. "
             "Only applies if --jpeg_lossy and --jpeg_progressive are set.");
DEFINE_string(jpeg_color_sampling,
              "RETAIN", "Color sampling to use. "
              "Only applies if --jpeg_lossy is set."
              "Valid values are RETAIN, YUV420, YUV422, YUV444.");
DEFINE_string(input_format, "", "Format of input image. "
              "If unspecified, format will be inferred from file extension. "
              "Valid values are JPEG, GIF, PNG.");

using pagespeed::image_compression::ColorSampling;
using pagespeed::image_compression::GifReader;
using pagespeed::image_compression::ImageConverter;
using pagespeed::image_compression::OptimizeJpeg;
using pagespeed::image_compression::OptimizeJpegWithOptions;
using pagespeed::image_compression::PngOptimizer;
using pagespeed::image_compression::PngReader;

namespace {

enum ImageType {
  NOT_SUPPORTED,
  JPEG,
  PNG,
  GIF,
};

pagespeed::image_compression::ColorSampling GetJpegColorSampling() {
  if (FLAGS_jpeg_color_sampling == "RETAIN") {
    return pagespeed::image_compression::RETAIN;
  } else if (FLAGS_jpeg_color_sampling == "YUV420") {
    return pagespeed::image_compression::YUV420;
  } else if (FLAGS_jpeg_color_sampling == "YUV422") {
    return pagespeed::image_compression::YUV422;
  } else if (FLAGS_jpeg_color_sampling == "YUV444") {
    return pagespeed::image_compression::YUV444;
  } else {
    LOG(DFATAL) << "Unrecognized color sampling. Using default.";
    return pagespeed::image_compression::RETAIN;
  }
}

pagespeed::image_compression::JpegCompressionOptions
    GetJpegCompressionOptions() {
  pagespeed::image_compression::JpegCompressionOptions options;
  options.lossy = FLAGS_jpeg_lossy;
  options.lossy_options.quality = FLAGS_jpeg_quality;
  options.lossy_options.color_sampling = GetJpegColorSampling();
  options.lossy_options.num_scans = FLAGS_jpeg_num_scans;
  options.progressive = FLAGS_jpeg_progressive;
  return options;
}

// use file extension to determine what optimizer should be used.
ImageType DetermineImageType(const std::string& filename) {
  if (!FLAGS_input_format.empty()) {
    if (FLAGS_input_format == "JPEG") return JPEG;
    if (FLAGS_input_format == "GIF") return GIF;
    if (FLAGS_input_format == "PNG") return PNG;
  } else {
    size_t dot_pos = filename.rfind('.');
    if (dot_pos != std::string::npos) {
      std::string extension;
      std::transform(filename.begin() + dot_pos + 1, filename.end(),
                     std::back_insert_iterator<std::string>(extension),
                     tolower);
      if (extension == "jpg" || extension == "jpeg") {
        return JPEG;
      } else if (extension == "png") {
        return PNG;
      } else if (extension == "gif") {
        return GIF;
      }
    }
  }
  return NOT_SUPPORTED;
}

bool ReadFileToString(const std::string& path, std::string *dest) {
  std::ifstream file_stream;
  file_stream.open(path.c_str(), std::ifstream::in | std::ifstream::binary);
  if (!file_stream.is_open()) {
    return false;
  }
  dest->assign(std::istreambuf_iterator<char>(file_stream),
               std::istreambuf_iterator<char>());
  file_stream.close();
  return (dest->size() > 0);
}

bool OptimizeImage(const std::string file_contents,
                   std::string* out_compressed) {
  ImageType type = DetermineImageType(FLAGS_input_file);
  if (type == NOT_SUPPORTED) {
    LOG(ERROR) << "Unable to determine input image type.";
    return false;
  }

  bool success = false;
  if (type == JPEG) {
    success = OptimizeJpegWithOptions(
        file_contents, out_compressed, GetJpegCompressionOptions());
  } else if (type == PNG) {
    PngReader reader;
    if (FLAGS_jpeg_lossy) {
      bool is_png;
      success = ImageConverter::OptimizePngOrConvertToJpeg(
          reader, file_contents, GetJpegCompressionOptions(),
          out_compressed, &is_png);
    } else {
      success = PngOptimizer::OptimizePngBestCompression(
          reader, file_contents, out_compressed);
    }
  } else if (type == GIF) {
    GifReader reader;
    success = PngOptimizer::OptimizePngBestCompression(reader,
                                                       file_contents,
                                                       out_compressed);
  } else {
    LOG(DFATAL) << "Unexpected image type: " << type;
    return false;
  }

  if (!success) {
    LOG(ERROR) << "Image compression failed when processing "
               << FLAGS_input_file;
    return false;
  }

  if (out_compressed->size() >= file_contents.size()) {
    *out_compressed = file_contents;
  }

  return true;
}

void PrintUsage() {
  ::google::ShowUsageWithFlagsRestrict(::google::GetArgv0(), __FILE__);
}

}  // namespace

int main(int argc, char** argv) {
  ::google::SetUsageMessage("Optimize a PNG, JPEG, or GIF image.");
  ::google::ParseCommandLineNonHelpFlags(&argc, &argv, true);

  std::string file_contents;
  if (FLAGS_input_file == "-") {
    // Special case: if user specifies input file as '-', read the
    // input from stdin.
    file_contents.assign(std::istreambuf_iterator<char>(std::cin),
                         std::istreambuf_iterator<char>());
  } else if (!ReadFileToString(FLAGS_input_file, &file_contents)) {
    fprintf(stderr, "Failed to read input file %s.\n",
            FLAGS_input_file.c_str());
    PrintUsage();
    return EXIT_FAILURE;
  }

  std::string compressed;
  bool result = OptimizeImage(file_contents, &compressed);
  if (!result) {
    PrintUsage();
    return EXIT_FAILURE;
  }

  if (compressed.size() == file_contents.size()) {
    printf("Unable to further optimize image %s.\n", FLAGS_input_file.c_str());
  } else {
    size_t savings = file_contents.size() - compressed.size();
    double percent_savings = 100.0 * savings / file_contents.size();
    printf("Reduced size of %s by %d bytes (%.1f%%).\n",
           FLAGS_input_file.c_str(),
           static_cast<int>(savings),
           percent_savings);
  }
  std::ofstream out(FLAGS_output_file.c_str(),
                    std::ios::out | std::ios::binary);
  if (!out) {
    fprintf(stderr, "Error opening %s for write.\n",
            FLAGS_output_file.c_str());
    PrintUsage();
    return EXIT_FAILURE;
  }
  out.write(compressed.c_str(), compressed.size());
  out.close();
  return EXIT_SUCCESS;
}
