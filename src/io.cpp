#include "io.h"

bool File::open(const char* filename) {
  m_file = fopen(filename, "rb");
  return m_file != nullptr;
}

void File::close() {
  if (m_file) {
    fclose(m_file);
    m_file = nullptr;
    m_position = 0;
  }
}

size_t File::size() const {
  if (!m_file) {
    return 0;
  }

  fseek(m_file, 0, SEEK_END);
  long sz = ftell(m_file);
  fseek(m_file, 0, SEEK_SET);

  return static_cast<size_t>(sz);
}

bool File::read(void* buf, size_t size) {
  fseek(m_file, static_cast<long>(m_position), SEEK_SET);
  size_t count = fread(buf, size, 1, m_file);

  bool ok = (count == 1);
  if (ok) {
    m_position += size;
  }

  return ok;
}
