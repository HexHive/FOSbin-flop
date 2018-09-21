//
// Created by derrick on 9/17/18.
//

#include "binaryDescriptor.h"
#include <fstream>
#include <string>
#include <algorithm>
#include <experimental/filesystem>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <binaryDescriptor.h>


namespace fs = std::experimental::filesystem;

fbf::BinaryDescriptor::BinaryDescriptor(fs::path path) :
        desc_path_(path) {
    if (fs::is_empty(desc_path_)) {
        throw std::runtime_error("Descriptor is empty");
    }

    if (fs::is_directory(desc_path_)) {
        throw std::runtime_error("Descriptor is a directory");
    }

    size_t line_num = 0;
    std::fstream f(path);
    std::string line;

    while (std::getline(f, line)) {
        line_num++;
        std::remove_if(line.begin(), line.end(), isspace);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        size_t index = line.find('=');
        if (index == std::string::npos) {
            std::string msg = "Invalid line at line ";
            msg += line_num;
            msg += ": ";
            msg += line;
            throw std::runtime_error(msg.c_str());
        }

        std::string key = line.substr(0, index);
        std::string val = line.substr(index + 1);

        if (key == "binary") {
            bin_path_ = val.c_str();
            if (!fs::exists(bin_path_)) {
                std::string msg = "Could not find binary at ";
                msg += val;
                throw std::runtime_error(msg.c_str());
            }
            continue;
        } else if (key == "addr") {
            uintptr_t addr = parse_offset(val);
            if (offsets_.find(addr) != offsets_.end()) {
                continue;
            }
            offsets_.insert(addr);
            continue;
        } else if (key == "data_size" ||
                   key == "bss_size") {
            size_t size = (size_t) parse_offset(val);
            if (key == "data_size") {
                data_.size_ = size;
            } else {
                bss_.size_ = size;
            }
            continue;
        } else if (key == "bss_location") {
            uintptr_t location = parse_offset(val);
            bss_.location_ = location;
        } else if (key == "bss_offset") {
            bss_.offset_ = parse_offset(val);
        } else if (key == "data_location") {
            uintptr_t location = parse_offset(val);
            data_.location_ = location;
        } else if (key == "data_offset") {
            data_.offset_ = parse_offset(val);
        } else {
            std::string msg = "Unknown key: ";
            msg += key;
            throw std::runtime_error(msg.c_str());
        }
    }

    if (bin_path_.empty() || offsets_.empty()) {
        throw std::runtime_error("Binary path and at least one offset required.");
    }

    struct stat st;

    int fd = open(bin_path_.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("Could not open binary");
    }
    if (fstat(fd, &st) < 0) {
        close(fd);
        throw std::runtime_error("Failed to get binary stats");
    }
    text_.size_ = st.st_size;

    void *offset = mmap(NULL, text_.size_,
                        PROT_EXEC | PROT_READ,
                        MAP_PRIVATE, fd, 0);

    if (offset == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("Failed to memory map binary");
    }
    close(fd);
    text_.location_ = (uintptr_t) offset;

    if (bss_.size_ > 0) {
        offset = mmap((void *) (text_.location_ + bss_.location_),
                      bss_.size_, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
        if (offset == MAP_FAILED) {
            char *err = strerror(errno);
            std::string msg = "Failed to memory map BSS: ";
            msg += err;
            throw std::runtime_error(msg);
        }
        bss_.location_ = (uintptr_t) offset;
        std::memcpy((void *) bss_.location_, (void *) (text_.location_ + bss_.offset_), bss_.size_);
    }

    if (data_.size_ > 0) {
        offset = mmap((void *) (text_.location_ + data_.location_),
                      data_.size_, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
        if (offset == MAP_FAILED) {
            char *err = strerror(errno);
            std::string msg = "Failed to memory map data: ";
            msg += err;
            throw std::runtime_error(msg);
        }
        data_.location_ = (uintptr_t) offset;
    }
}


uintptr_t fbf::BinaryDescriptor::parse_offset(std::string &offset) {
    std::istringstream iss(offset);
    uintptr_t addr = 0;
    iss >> std::hex >> addr;
    return addr;
}

fbf::BinaryDescriptor::~BinaryDescriptor() {
    if (text_.location_ > 0) {
        munmap((void *) text_.location_, text_.size_);
    }

    if (data_.location_ > 0) {
        munmap((void *) data_.location_, data_.size_);
    }

    if (bss_.location_ > 0) {
        munmap((void *) bss_.location_, bss_.size_);
    }
}

fbf::BinSection &fbf::BinaryDescriptor::getText() {
    return text_;
}

fbf::BinSection &fbf::BinaryDescriptor::getData() {
    return data_;
}

fbf::BinSection &fbf::BinaryDescriptor::getBss() {
    return bss_;
}

fs::path &fbf::BinaryDescriptor::getPath() {
    return bin_path_;
}

std::set<uintptr_t> fbf::BinaryDescriptor::getOffsets() {
    return offsets_;
}
