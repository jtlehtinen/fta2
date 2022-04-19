#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

class File {
  private:
    FILE* m_file = nullptr;
    size_t m_position = 0;

  public:
    bool open(const char* filename);
    void close();

    size_t size() const;
    bool read(void* buf, size_t size);
};

struct Reader {
  uint8_t* data;
  size_t size;
  size_t cursor;

  Reader(void* data, size_t size) : data(reinterpret_cast<uint8_t*>(data)), size(size), cursor(0) { }

  bool done() const {
    return cursor >= size;
  }

  template <typename T>
  T* get_ptr() const {
    return reinterpret_cast<T*>(data + cursor);
  }

  template <typename T>
  T read() {
    assert(cursor + sizeof(T) <= size);
    T r = *get_ptr<T>();
    cursor += sizeof(T);
    return r;
  }

  template <typename T>
  void read_many(void* dest, size_t count) {
    assert(cursor + sizeof(T) * count <= size);
    memcpy(dest, data + cursor, count * sizeof(T));
    cursor += sizeof(T) * count;
  }

  void skip(size_t bytes) {
    cursor += bytes;
  }
};
