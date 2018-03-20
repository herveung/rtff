#include <gtest/gtest.h>

#include <iostream>

#include <Eigen/Core>

#include "rtff/abstract_filter.h"
#include "rtff/filter.h"
#include "wave/file.h"

const std::string gResourcePath(TEST_RESOURCES_PATH);

class MyFilter : public rtff::AbstractFilter {
 private:
  void ProcessTransformedBlock(std::vector<std::complex<float>*> data,
                               uint32_t size) override {
    for (uint8_t channel_idx = 0; channel_idx < data.size(); channel_idx++) {
      auto buffer = Eigen::Map<Eigen::VectorXcf>(data[channel_idx], size);
      ASSERT_EQ(size, fft_size() / 2 + 1);
      buffer.block(20, 0, 50, 1) *= 0;
    }
  }
};

TEST(RTFF, Basis) {
  // Read input file content
  wave::File file;
  file.Open(gResourcePath + "/Untitled3.wav", wave::OpenMode::kIn);
  std::vector<float> content(file.frame_number() * file.channel_number());
  ASSERT_EQ(file.Read(&content), wave::Error::kNoError);

  // Initialize filter
  auto block_size = 2048;
  auto channel_number = file.channel_number();

  MyFilter filter;
  std::error_code err;
  filter.Init(channel_number, err);
  ASSERT_FALSE(err);
  filter.set_block_size(block_size);

  rtff::AudioBuffer buffer;
  buffer.Init(block_size, channel_number);

  // For debug. From this point, the application shouldn't allocate any memory.
  Eigen::internal::set_is_malloc_allowed(false);

  // Extract each frames (add latency)
  auto multichannel_buffer_size = block_size * channel_number;

  for (uint32_t sample_idx = 0;
       sample_idx < content.size() - multichannel_buffer_size;
       sample_idx += multichannel_buffer_size) {
    float* sample_ptr = content.data() + sample_idx;
    memcpy(buffer.data(), sample_ptr,
           block_size * channel_number * sizeof(float));

    filter.ProcessBlock(&buffer);

    memcpy(sample_ptr, buffer.data(),
           block_size * channel_number * sizeof(float));

    // display the current status
    std::cout << round(double(sample_idx * 100) /
                       (file.frame_number() * file.channel_number()))
              << "%" << std::endl;
  }

  // For debug. From this point, the application can allocate memory
  Eigen::internal::set_is_malloc_allowed(true);

  // Write the output file content
  wave::File output;
  output.Open("/tmp/rtff_res.wav", wave::OpenMode::kOut);
  output.set_sample_rate(file.sample_rate());
  output.set_channel_number(file.channel_number());
  output.set_bits_per_sample(file.bits_per_sample());
  output.Write(content);
}

// This test makes sure that changing the block size doesn't crash the process
TEST(RTFF, ChangeBlockSize) {
  MyFilter filter;
  std::error_code err;
  auto channel_number = 1;
  filter.Init(channel_number, err);
  ASSERT_FALSE(err);

  auto block_size = 512;
  filter.set_block_size(block_size);

  rtff::AudioBuffer buffer;
  buffer.Init(block_size, channel_number);
  // queue 50 buffer
  for (auto index = 0; index < 50; index++) {
    memset(buffer.data(), 0, block_size);
    filter.ProcessBlock(&buffer);
  }
  block_size = 1024;
  filter.set_block_size(block_size);
  buffer.Init(block_size, channel_number);
  // queue 50 other buffer
  for (auto index = 0; index < 50; index++) {
    memset(buffer.data(), 0, block_size);
    filter.ProcessBlock(&buffer);
  }
}

// this test makes sure that filter class syntax is functionnal
TEST(RTFF, Filter) {
  rtff::Filter filter;
  std::error_code err;
  auto channel_number = 1;
  filter.Init(channel_number, err);
  filter.execute = [](std::vector<std::complex<float>*> data, uint32_t size) {
    for (uint8_t channel_idx = 0; channel_idx < data.size(); channel_idx++) {
      auto buffer = Eigen::Map<Eigen::VectorXcf>(data[channel_idx], size);
      buffer = Eigen::VectorXcf::Random(size);
    }
  };

  ASSERT_FALSE(err);
  auto block_size = 512;
  filter.set_block_size(block_size);

  rtff::AudioBuffer buffer;
  buffer.Init(block_size, channel_number);
  // queue 50 buffer
  for (auto index = 0; index < 50; index++) {
    memset(buffer.data(), 0, block_size);
    filter.ProcessBlock(&buffer);
  }
}

uint32_t GetLatency(rtff::Filter& filter);

TEST(RTFF, Latency) {
  rtff::Filter filter;
  std::error_code err;
  filter.Init(1, err);

  // case blocksize % fftsize == 0
  filter.set_block_size(512);
  ASSERT_EQ(filter.FrameLatency(), GetLatency(filter));

  // case block size > fft size
  filter.set_block_size(filter.fft_size() + 100);
  ASSERT_EQ(filter.FrameLatency(), GetLatency(filter));

  // case block_size < fft_size and blocksize % fftsize != 0
  filter.set_block_size(filter.fft_size() - 100);
  ASSERT_EQ(filter.FrameLatency(), GetLatency(filter));
}

// Compute the filter latency by sending a Dirac and checking the filter output
uint32_t GetLatency(rtff::Filter& filter) {
  rtff::AudioBuffer buffer;
  buffer.Init(filter.block_size(), filter.channel_count());
  auto block_size = filter.block_size();

  // generate a dirac
  auto sample_rate = 44100;
  auto pre_dirac_samples = sample_rate * 1;
  std::vector<float> content(pre_dirac_samples * 2 + 1, 0);
  content[pre_dirac_samples] = 1;

  // run data into the filter
  for (uint32_t sample_idx = 0; sample_idx < content.size() - block_size;
       sample_idx += block_size) {
    float* sample_ptr = content.data() + sample_idx;
    memcpy(buffer.data(), sample_ptr, block_size * sizeof(float));

    filter.ProcessBlock(&buffer);

    memcpy(sample_ptr, buffer.data(), block_size * sizeof(float));
  }
  uint32_t max_index = 0;
  Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, 1>>(content.data(),
                                                            content.size())
      .maxCoeff(&max_index);
  auto latency = max_index - pre_dirac_samples;
  return latency;
}
