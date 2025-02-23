// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/devtools/device/usb/android_usb_socket.h"

#include <stddef.h>

#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "net/base/net_errors.h"

namespace {

const int kMaxPayload = 4096;

}  // namespace

AndroidUsbSocket::AndroidUsbSocket(scoped_refptr<AndroidUsbDevice> device,
                                   uint32_t socket_id,
                                   const std::string& command,
                                   base::Closure delete_callback)
    : device_(device),
      command_(command),
      local_id_(socket_id),
      remote_id_(0),
      is_connected_(false),
      delete_callback_(delete_callback),
      weak_factory_(this) {}

AndroidUsbSocket::~AndroidUsbSocket() {
  DCHECK(CalledOnValidThread());
  if (is_connected_)
    Disconnect();
  if (!delete_callback_.is_null())
    delete_callback_.Run();
}

void AndroidUsbSocket::HandleIncoming(scoped_ptr<AdbMessage> message) {
  if (!device_.get())
    return;

  CHECK_EQ(message->arg1, local_id_);
  switch (message->command) {
    case AdbMessage::kCommandOKAY:
      if (!is_connected_) {
        remote_id_ = message->arg0;
        is_connected_ = true;
        if (!connect_callback_.is_null())
          base::ResetAndReturn(&connect_callback_).Run(net::OK);
        // "this" can be deleted.
      } else {
        RespondToWriter(write_length_);
        // "this" can be deleted.
      }
      break;
    case AdbMessage::kCommandWRTE:
      device_->Send(AdbMessage::kCommandOKAY, local_id_, message->arg0, "");
      read_buffer_ += message->body;
      // Allow WRTE over new connection even though OKAY ack was not received.
      if (!is_connected_) {
        remote_id_ = message->arg0;
        is_connected_ = true;
        if (!connect_callback_.is_null())
          base::ResetAndReturn(&connect_callback_).Run(net::OK);
        // "this" can be deleted.
      } else {
        RespondToReader(false);
        // "this" can be deleted.
      }
      break;
    case AdbMessage::kCommandCLSE:
      if (is_connected_)
        device_->Send(AdbMessage::kCommandCLSE, local_id_, 0, "");
      Terminated(true);
      // "this" can be deleted.
      break;
    default:
      break;
  }
}

void AndroidUsbSocket::Terminated(bool closed_by_device) {
  is_connected_ = false;

  // Break the socket -> device connection, release the device.
  device_ = nullptr;
  base::ResetAndReturn(&delete_callback_).Run();

  if (!closed_by_device)
    return;

  // Respond to pending callbacks.
  if (!connect_callback_.is_null()) {
    base::ResetAndReturn(&connect_callback_).Run(net::ERR_FAILED);
    // "this" can be deleted.
    return;
  }
  base::WeakPtr<AndroidUsbSocket> weak_this = weak_factory_.GetWeakPtr();
  RespondToReader(true);
  // "this" can be deleted.
  if (weak_this) {
    RespondToWriter(net::ERR_FAILED);
    // "this" can be deleted.
  }
}

int AndroidUsbSocket::Read(net::IOBuffer* buffer,
                           int length,
                           const net::CompletionCallback& callback) {
  DCHECK(!callback.is_null());
  if (!is_connected_)
    return device_.get() ? net::ERR_SOCKET_NOT_CONNECTED : 0;

  DCHECK(read_callback_.is_null());
  if (read_buffer_.empty()) {
    read_callback_ = callback;
    read_io_buffer_ = buffer;
    read_length_ = length;
    return net::ERR_IO_PENDING;
  }

  size_t bytes_to_copy = static_cast<size_t>(length) > read_buffer_.length() ?
      read_buffer_.length() : static_cast<size_t>(length);
  memcpy(buffer->data(), read_buffer_.data(), bytes_to_copy);
  if (read_buffer_.length() > bytes_to_copy)
    read_buffer_ = read_buffer_.substr(bytes_to_copy);
  else
    read_buffer_ = std::string();
  return bytes_to_copy;
}

int AndroidUsbSocket::Write(net::IOBuffer* buffer,
                            int length,
                            const net::CompletionCallback& callback) {
  DCHECK(!callback.is_null());
  if (!is_connected_)
    return net::ERR_SOCKET_NOT_CONNECTED;

  if (length > kMaxPayload)
    length = kMaxPayload;

  DCHECK(write_callback_.is_null());
  write_callback_ = callback;
  write_length_ = length;
  device_->Send(AdbMessage::kCommandWRTE, local_id_, remote_id_,
                std::string(buffer->data(), length));
  return net::ERR_IO_PENDING;
}

int AndroidUsbSocket::SetReceiveBufferSize(int32_t size) {
  NOTIMPLEMENTED();
  return net::ERR_NOT_IMPLEMENTED;
}

int AndroidUsbSocket::SetSendBufferSize(int32_t size) {
  NOTIMPLEMENTED();
  return net::ERR_NOT_IMPLEMENTED;
}

int AndroidUsbSocket::Connect(const net::CompletionCallback& callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(!callback.is_null());
  if (!device_.get())
    return net::ERR_FAILED;

  DCHECK(!is_connected_);
  DCHECK(connect_callback_.is_null());
  connect_callback_ = callback;
  device_->Send(AdbMessage::kCommandOPEN, local_id_, 0, command_);
  return net::ERR_IO_PENDING;
}

void AndroidUsbSocket::Disconnect() {
  if (!device_.get())
    return;
  device_->Send(AdbMessage::kCommandCLSE, local_id_, remote_id_, "");
  Terminated(false);
}

bool AndroidUsbSocket::IsConnected() const {
  DCHECK(CalledOnValidThread());
  return is_connected_;
}

bool AndroidUsbSocket::IsConnectedAndIdle() const {
  NOTIMPLEMENTED();
  return false;
}

int AndroidUsbSocket::GetPeerAddress(net::IPEndPoint* address) const {
  net::IPAddressNumber ip(net::kIPv4AddressSize);
  *address = net::IPEndPoint(ip, 0);
  return net::OK;
}

int AndroidUsbSocket::GetLocalAddress(net::IPEndPoint* address) const {
  NOTIMPLEMENTED();
  return net::ERR_NOT_IMPLEMENTED;
}

const net::BoundNetLog& AndroidUsbSocket::NetLog() const {
  return net_log_;
}

void AndroidUsbSocket::SetSubresourceSpeculation() {
  NOTIMPLEMENTED();
}

void AndroidUsbSocket::SetOmniboxSpeculation() {
  NOTIMPLEMENTED();
}

bool AndroidUsbSocket::WasEverUsed() const {
  NOTIMPLEMENTED();
  return true;
}

bool AndroidUsbSocket::UsingTCPFastOpen() const {
  NOTIMPLEMENTED();
  return true;
}

bool AndroidUsbSocket::WasNpnNegotiated() const {
  NOTIMPLEMENTED();
  return true;
}

net::NextProto AndroidUsbSocket::GetNegotiatedProtocol() const {
  NOTIMPLEMENTED();
  return net::kProtoUnknown;
}

bool AndroidUsbSocket::GetSSLInfo(net::SSLInfo* ssl_info) {
  return false;
}

void AndroidUsbSocket::GetConnectionAttempts(
    net::ConnectionAttempts* out) const {
  out->clear();
}

int64_t AndroidUsbSocket::GetTotalReceivedBytes() const {
  NOTIMPLEMENTED();
  return 0;
}

void AndroidUsbSocket::RespondToReader(bool disconnect) {
  if (read_callback_.is_null() || (read_buffer_.empty() && !disconnect))
    return;
  size_t bytes_to_copy =
      static_cast<size_t>(read_length_) > read_buffer_.length() ?
          read_buffer_.length() : static_cast<size_t>(read_length_);
  memcpy(read_io_buffer_->data(), read_buffer_.data(), bytes_to_copy);
  if (read_buffer_.length() > bytes_to_copy)
    read_buffer_ = read_buffer_.substr(bytes_to_copy);
  else
    read_buffer_ = std::string();
  base::ResetAndReturn(&read_callback_).Run(bytes_to_copy);
}

void AndroidUsbSocket::RespondToWriter(int result) {
  if (!write_callback_.is_null())
    base::ResetAndReturn(&write_callback_).Run(result);
}
