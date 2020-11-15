#include "opencv2/opencv.hpp"
#include <chrono>
#include <iostream>
#include <map>
#include <thread>

#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/udp.hpp>

#include "LockFreeSpsc.h"
#include "Logger.h"
#include "OpenCVUtils.h"
#include "TimeLogger.h"
#include "VideoWindow.h"
#include "InputBuffer.h"

struct FrameStitcher
{
  explicit FrameStitcher(int parts_num_)
      : _parts_num(parts_num_), _image_buffer(5 * MB)
  {
  }

  FrameStitcher(FrameStitcher&&) = default;

  void add(const InputBuffer::Header& h_, ::asio::const_buffer part_)
  {
    assert(_parts_num > 0);
    assert(h_.part_begin + part_.size() <= _image_buffer.size());

    memcpy(_image_buffer.data() + h_.part_begin, part_.data(), part_.size());
    _parts_num--;
  }

  bool is_complete() const { return _parts_num == 0; }

  cv::Mat decoded() const
  {
    assert(is_complete());
    return cv::imdecode(_image_buffer, cv::IMREAD_UNCHANGED);
  }

  private:
  int _parts_num;
  std::vector<uchar> _image_buffer;
};

struct FramesManager
{
  void add(const InputBuffer::Header& h_, ::asio::const_buffer part_)
  {
    if (h_.frame_id < _last_frame_id - 2)
    {
      std::cerr << "too old " << h_.frame_id << std::endl;
      // Too old, throw it away
      return;
    }
    else
    {
      //      std::cerr << "got frame id " << h_.frame_id << "_last_frame id "
      //      << _last_frame_id << std::endl;
      // Throw away the old one and start with the new one
      _last_frame_id = std::max(_last_frame_id, h_.frame_id);
      _frames.erase(_last_frame_id - 2);
    }

    auto frameIter = _frames.find(h_.frame_id);
    if (frameIter == _frames.cend())
    {
      TimeLogger t("New frame found", std::cout);
      frameIter = _frames
                      .insert(std::make_pair(h_.frame_id,
                                             FrameStitcher(h_.total_parts)))
                      .first;
    }

    auto& frameStitcher = frameIter->second;

    frameStitcher.add(h_, part_);

    if (frameStitcher.is_complete())
    {
      if (h_.frame_id >= _last_frame_id)
      {
        _last_complete_frame = h_.frame_id;
      }
      else
      {
        // It's stale, throw it away
        _frames.erase(h_.frame_id);
      }
    }
  }

  bool is_frame_ready() const { return _last_complete_frame != -1; }

  cv::Mat get_last_frame()
  {
    assert(_last_complete_frame > -1);
    FrameStitcher frame_stitcher = std::move(_frames.at(_last_complete_frame));
    Logger::Debug("Decoded frame", _last_complete_frame);
    _last_complete_frame = -1;
    return frame_stitcher.decoded();
  }

  private:
  int _last_complete_frame{-1};
  int _last_frame_id{};
  std::map<int, FrameStitcher> _frames;
};

void receiver()
{
  try
  {
    ::asio::io_context ioContext;

    ::asio::ip::udp::resolver resolver(ioContext);

    ::asio::ip::udp::socket recv_socket(ioContext);

    const std::string recv_address("0.0.0.0");
    const int recv_port = 39009;

    InputBuffer input_buffer;

    ::asio::ip::udp::endpoint recv_endpoint(::asio::ip::udp::v4(), recv_port);

    recv_socket.open(::asio::ip::udp::v4());

    recv_socket.bind(recv_endpoint);

    LockFreeSpsc<InputBuffer> disruptor(10000);

    struct Handler
    {
      Handler(::asio::ip::udp::socket& socket_,
              InputBuffer& input_buffer_,
              LockFreeSpsc<InputBuffer>& disruptor_)
          : _socket(socket_),
            _input_buffer(input_buffer_),
            _disruptor(disruptor_)
      {
      }

      void operator()(asio::error_code err, std::size_t)
      {
        if (err)
        {
          Logger::Error("Recv Err ", err.message());
        }
        else
        {
          static bool frame_dropped{};
          InputBuffer::Header header = _input_buffer.get_header();

          if (header.part_id == 0)
          {
            // Try again with new frame
            frame_dropped = false;
          }

          if (!frame_dropped && !_disruptor.try_push(std::move(_input_buffer)))
          {
            // Couldn't insert this part, let's skip all the rest of the parts
            // until the next frame
            frame_dropped = true;
          }

          _socket.async_receive(_input_buffer.data(), *this);
        }
      }

  private:
      ::asio::ip::udp::socket& _socket;
      InputBuffer& _input_buffer;
      LockFreeSpsc<InputBuffer>& _disruptor;
    };

    Handler handler(recv_socket, input_buffer, disruptor);
    recv_socket.async_receive(input_buffer.data(), handler);

    std::thread display_frame_thread([&disruptor] {
      try
      {
        while (true)
        {
          static FramesManager frame_manager;
          static cv::Mat frame;

          InputBuffer part_buf;
          while (disruptor.try_pop(part_buf))
          {
            auto [header, part] = part_buf.parse();
            Logger::Debug("Received", header);

            frame_manager.add(header, part);

            if (frame_manager.is_frame_ready())
            {
              Logger::Debug("Updating Frame");
              frame = frame_manager.get_last_frame();
            }
          }

          if (!frame.empty())
          {
            opencv_utils::displayMat(frame, "recv");
          }

          // Press  ESC on keyboard to  exit
          const char c = static_cast<char>(cv::waitKey(1));
          std::this_thread::sleep_for(std::chrono::milliseconds(16));
          if (c == 27)
          {
            break;
          }
        }
      }
      catch (const std::exception& e_)
      {
        Logger::Error("Receiver Error: ", e_.what());
      }
    });

    Logger::Info("Waiting for connections on",
                 recv_endpoint.address().to_string(),
                 recv_endpoint.port());
    ioContext.run();
    if (display_frame_thread.joinable())
    {
      display_frame_thread.join();
    }
  }
  catch (const std::exception& e_)
  {
    Logger::Error("Receiver Error: ", e_.what());
  }
}

int main(/*int argc, char* argv[]*/)
{
  Logger::SetLevel(Logger::INFO);
  //    if (argc < 2)
  //    {
  //        std::cerr << "Please add the remote address " << std::endl;
  //        return -1;
  //    }
  //    const std::string recv_address(argv[1]);

  receiver();

  return 0;
}
