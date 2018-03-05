#include "rtff/buffer/ring_buffer.h"

#include "rtff/buffer/audio_buffer.h"
#include "rtff/buffer/buffer.h"

namespace rtff {

RingBuffer::RingBuffer(uint32_t write_size, uint32_t read_size,
                       uint32_t step_size, uint8_t channel_count) {
  write_size_ = write_size * channel_count;
  read_size_ = read_size * channel_count;
  step_size_ = step_size * channel_count;
  write_index_ = 0;
  read_index_ = 0;
  available_data_size_ = 0;
  channel_count_ = channel_count;

  buffer_.resize(std::max(read_size_, write_size_) * 2);
  // used in RingBuffer::Read(AmplitudeBuffer* buffer)
  temp_read_data_.resize(read_size * channel_count);
  // used in RingBuffer::Write(const AmplitudeBuffer& buffer)
  temp_write_data_.resize(write_size * channel_count);
}

void RingBuffer::Write(const float* data) {
  if (write_index_ + write_size_ > buffer_.size()) {
    // When we reach the end of the buffer
    auto remaining_size = buffer_.size() - write_index_;
    std::copy(data, data + remaining_size, buffer_.data() + write_index_);
    std::copy(data + remaining_size, data + write_size_, buffer_.data());
    write_index_ = (write_size_ - remaining_size);
  } else {
    // we have enough size remaining
    std::copy(data, data + write_size_, buffer_.data() + write_index_);
    write_index_ += write_size_;
  }
  available_data_size_ += write_size_;
}

void RingBuffer::Write(const AudioBuffer& buffer) {
  // TODO: assert frame_count == write_size
  Write(buffer.data());
}
void RingBuffer::Write(const Buffer<float>& buffer) {
  Eigen::Map<Eigen::MatrixXf> write_data(
      temp_write_data_.data(), channel_count_, write_size_ / channel_count_);
  for (uint8_t channel_idx = 0; channel_idx < channel_count_; channel_idx++) {
    write_data.row(channel_idx) = buffer.channel(channel_idx);
  }
  Write(write_data.data());
}

bool RingBuffer::Read(float* data) {
  if (available_data_size_ < read_size_) {
    return 0;
  }

  if (read_index_ + read_size_ > buffer_.size()) {
    auto remaining_size = buffer_.size() - read_index_;
    std::copy(buffer_.data() + read_index_,
              buffer_.data() + read_index_ + remaining_size, data);
    std::copy(buffer_.data(), buffer_.data() + (read_size_ - remaining_size),
              data + remaining_size);
    read_index_ += step_size_;
    if (read_index_ > buffer_.size()) {
      read_index_ -= buffer_.size();
    }
  } else {
    // default read
    std::copy(buffer_.data() + read_index_,
              buffer_.data() + read_index_ + read_size_, data);
    read_index_ += step_size_;
  }
  available_data_size_ -= step_size_;

  return read_size_;
}

bool RingBuffer::Read(Buffer<float>* buffer) {
  if (!Read(temp_read_data_.data())) {
    return false;
  }
  Eigen::Map<Eigen::MatrixXf> read_data(temp_read_data_.data(), channel_count_,
                                        read_size_ / channel_count_);
  for (uint8_t channel_idx = 0; channel_idx < channel_count_; channel_idx++) {
    buffer->channel(channel_idx).noalias() = read_data.row(channel_idx);
  }
  return true;
}

}  // namespace rtff
