#include "rtff/abstract_filter.h"

#include "rtff/buffer/buffer.h"
#include "rtff/filter_impl.h"

#include <iostream>

namespace rtff {

class AbstractFilter::Impl {
 public:
  TimeAmplitudeBuffer amplitude_block;
  TimeAmplitudeBuffer output_amplitude_block;
  TimeFrequencyBuffer frequential_block;
};

AbstractFilter::AbstractFilter()
    : fft_size_(2048), overlap_(2048 * 0.5), block_size_(512) {}

void AbstractFilter::Init(uint8_t channel_count, uint32_t fft_size,
                          uint32_t overlap, std::error_code& err) {
  fft_size_ = fft_size;
  overlap_ = overlap;
  Init(channel_count, err);
}

void AbstractFilter::Init(uint8_t channel_count, std::error_code& err) {
  channel_count_ = channel_count;
  InitBuffers();

  // init single block buffers
  buffers_ = std::make_shared<Impl>();
  buffers_->amplitude_block.Init(fft_size(), channel_count);
  buffers_->output_amplitude_block.Init(hop_size(), channel_count);
  buffers_->frequential_block.Init(fft_size() / 2 + 1, channel_count);

  impl_ = std::make_shared<FilterImpl>();
  impl_->Init(fft_size(), overlap(), channel_count, err);
  if (err) {
    return;
  }
}

void AbstractFilter::InitBuffers() {
  input_buffer_ = std::make_shared<RingBuffer>(block_size(), fft_size(),
                                               hop_size(), channel_count());
  output_buffer_ = std::make_shared<RingBuffer>(hop_size(), block_size(),
                                                block_size(), channel_count());
  
  // initialize the intput_buffer_ with hop_size frames of zeros
  if (fft_size() > block_size()) {
    std::vector<float> zeros;
    zeros.resize((fft_size() - block_size()) * channel_count(), 0);
    input_buffer_->Write(zeros.data(), zeros.size());
  }
}

void AbstractFilter::set_block_size(uint32_t value) {
  block_size_ = value;
  InitBuffers();
}
uint32_t AbstractFilter::block_size() const { return block_size_; }
uint8_t AbstractFilter::channel_count() const { return channel_count_; }

uint32_t AbstractFilter::window_size() const { return impl_->window_size(); }
uint32_t AbstractFilter::fft_size() const { return fft_size_; }
uint32_t AbstractFilter::overlap() const { return overlap_; }
uint32_t AbstractFilter::hop_size() const { return fft_size_ - overlap_; }

uint32_t AbstractFilter::FrameLatency() const { return 0; }

void AbstractFilter::ProcessBlock(AudioBuffer* buffer) {
  input_buffer_->Write(*buffer);

  // process as many blocks as possible
  while (input_buffer_->Read(&(buffers_->amplitude_block))) {
    impl_->Analyze(buffers_->amplitude_block, &(buffers_->frequential_block));
    ProcessTransformedBlock(buffers_->frequential_block.data_ptr(),
                            buffers_->frequential_block.size());
    impl_->Synthesize(buffers_->frequential_block,
                      &(buffers_->output_amplitude_block));
    output_buffer_->Write(buffers_->output_amplitude_block);
  }

  if (output_buffer_->Read(buffer->data())) {
    return;
  }
  // if we don't have enough data to be read, just fill with zeros
  std::fill(buffer->data(),
            buffer->data() + buffer->channel_count() * buffer->frame_count(),
            0);
}

}  // namespace rtff
