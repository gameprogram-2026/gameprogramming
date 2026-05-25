#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// PacketWriter — linear write buffer for building outgoing packets
// ─────────────────────────────────────────────────────────────────────────────
class PacketWriter {
public:
    explicit PacketWriter(size_t reserve = 256) { m_buf.reserve(reserve); }

    template<typename T>
    PacketWriter& write(const T& val) {
        static_assert(std::is_trivially_copyable_v<T>);
        const size_t off = m_buf.size();
        m_buf.resize(off + sizeof(T));
        std::memcpy(m_buf.data() + off, &val, sizeof(T));
        return *this;
    }

    PacketWriter& writeBytes(const void* src, size_t len) {
        const size_t off = m_buf.size();
        m_buf.resize(off + len);
        std::memcpy(m_buf.data() + off, src, len);
        return *this;
    }

    const uint8_t* data() const noexcept { return m_buf.data(); }
    size_t         size() const noexcept { return m_buf.size(); }
    void           clear()     noexcept { m_buf.clear(); }

private:
    std::vector<uint8_t> m_buf;
};

// ─────────────────────────────────────────────────────────────────────────────
// PacketReader — linear read buffer for parsing incoming packets
// ─────────────────────────────────────────────────────────────────────────────
class PacketReader {
public:
    PacketReader(const uint8_t* data, size_t len)
        : m_data(data), m_len(len), m_pos(0) {}

    template<typename T>
    bool read(T& out) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (m_pos + sizeof(T) > m_len) { m_error = true; return false; }
        std::memcpy(&out, m_data + m_pos, sizeof(T));
        m_pos += sizeof(T);
        return true;
    }

    template<typename T>
    T readOr(T def) {
        T val{};
        return read(val) ? val : def;
    }

    bool skip(size_t n) {
        if (m_pos + n > m_len) { m_error = true; return false; }
        m_pos += n;
        return true;
    }

    size_t remaining() const noexcept { return m_len - m_pos; }
    bool   hasError()  const noexcept { return m_error; }
    bool   atEnd()     const noexcept { return m_pos >= m_len; }
    size_t pos()       const noexcept { return m_pos; }

private:
    const uint8_t* m_data;
    size_t         m_len;
    size_t         m_pos;
    bool           m_error = false;
};

} // namespace dz
