//
// Created by derrick on 2/21/19.
//
#include "ZergCommand.h"

/* This value gives each GP register up to 5 allocated areas
 * and enough room for their values along with RAX
 */
#define ZMSG_MAX_DATA_LEN   6 * 5 * DEFAULT_ALLOCATION_SIZE + 7 * sizeof(uintptr_t)

ZergMessage::ZergMessage(zerg_message_t type) :
        _message_type(type), _length(0), _data(nullptr), _self_allocated_data(false) {}

ZergMessage::ZergMessage(zerg_message_t type, size_t length, void *data) :
        _message_type(type), _length(length), _data(data), _self_allocated_data(false) {
    if (_length > ZMSG_MAX_DATA_LEN) {
        _length = ZMSG_MAX_DATA_LEN;
    }
}

ZergMessage::ZergMessage(const ZergMessage &msg) {
    _message_type = msg._message_type;
    _length = msg._length;

    if (_length > 0) {
        _self_allocated_data = true;
        _data = malloc(_length);
        memcpy(_data, msg._data, _length);
    } else {
        _data = nullptr;
        _self_allocated_data = false;
    }
}

ZergMessage::~ZergMessage() {
    if (_self_allocated_data) {
        free(_data);
    }
}

size_t ZergMessage::size() const { return _length; }

zerg_message_t ZergMessage::type() const { return _message_type; }

void *ZergMessage::data() const { return _data; }

const char *ZergMessage::str() {
    switch (_message_type) {
        case ZMSG_FAIL:
            return "ZMSG_FAIL";
        case ZMSG_OK:
            return "ZMSG_OK";
        case ZMSG_SET_TGT:
            return "ZMSG_SET_TGT";
        case ZMSG_EXIT:
            return "ZMSG_EXIT";
        case ZMSG_FUZZ:
            return "ZMSG_FUZZ";
        case ZMSG_EXECUTE:
            return "ZMSG_EXECUTE";
        case ZMSG_SET_CTX:
            return "ZMSG_SET_CTX";
        case ZMSG_RESET:
            return "ZMSG_RESET";
        case ZMSG_ACK:
            return "ZMSG_ACK";
        default:
            return "UNKNOWN MESSAGE";
    }
}

size_t ZergMessage::write_to_fd(int fd) const {
    size_t written = 0;
    int tmp = write(fd, &_message_type, sizeof(_message_type));
    if (tmp <= 0) {
        return 0;
    }

    written += tmp;
    tmp = write(fd, &_length, sizeof(_length));
    if (tmp <= 0) {
        return 0;
    }

    written += tmp;
    if (_length > 0) {
        tmp = write(fd, _data, _length);
        if (tmp <= 0) {
            return 0;
        }
        written += tmp;
    }

    fsync(fd);
    return written;
}

size_t ZergMessage::read_from_fd(int fd) {
    size_t read_bytes = 0;
    zerg_message_t msg_type;
    size_t length;
    void *data = nullptr;

    int tmp = read(fd, &msg_type, sizeof(msg_type));
    if (tmp <= 0) {
        return 0;
    }
    read_bytes += tmp;

    tmp = read(fd, &length, sizeof(length));
    if (tmp <= 0) {
        return 0;
    }
    read_bytes += tmp;

    if (length > 0) {
        if (length > ZMSG_MAX_DATA_LEN) {
            length = ZMSG_MAX_DATA_LEN;
        }

        data = malloc(length);
        if (!data) {
            return 0;
        }

        tmp = read(fd, data, length);
        if (read <= 0) {
            free(data);
            return 0;
        }
        read_bytes += tmp;
    }

    _message_type = msg_type;
    _length = length;
    if (data) {
        _data = data;
        _self_allocated_data = true;
    } else {
        _self_allocated_data = false;
    }

    return read_bytes;
}

size_t ZergMessage::add_contexts(const FBZergContext &pre, const FBZergContext &post) {
    /* Do not overwrite data if there is already some */
    if (_length > 0) {
        return 0;
    }

    std::stringstream data(std::ios::in | std::ios::out | std::ios::binary);
    data << pre;
    data << post;

    if (data.str().size() > ZMSG_MAX_DATA_LEN) {
        return 0;
    }

    _data = malloc(data.str().size());
    if (!_data) {
        return 0;
    }

    _length = data.str().size();
    memcpy(_data, data.str().c_str(), _length);
    return _length;
}
