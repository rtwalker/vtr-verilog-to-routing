#include "telegramheader.h"
#include "convertutils.h"

#include <sstream>

namespace comm {

TelegramHeader::TelegramHeader(uint32_t length, uint32_t check_sum, uint8_t compressor_id)
    : m_body_bytes_num(length)
    , m_body_check_sum(check_sum)
    , m_compressor_id(compressor_id) {
    m_buffer.resize(TelegramHeader::size());

    // Write signature into a buffer
    std::memcpy(m_buffer.data(), TelegramHeader::SIGNATURE, TelegramHeader::SIGNATURE_SIZE);

    // Write the length into the buffer in big-endian byte order
    std::memcpy(m_buffer.data() + TelegramHeader::LENGTH_OFFSET, &length, TelegramHeader::LENGTH_SIZE);

    // Write the checksum into the buffer in big-endian byte order
    std::memcpy(m_buffer.data() + TelegramHeader::CHECKSUM_OFFSET, &check_sum, TelegramHeader::CHECKSUM_SIZE);

    // Write compressor id
    std::memcpy(m_buffer.data() + TelegramHeader::COMPRESSORID_OFFSET, &compressor_id, TelegramHeader::COMPRESSORID_SIZE);

    m_is_valid = true;
}

TelegramHeader::TelegramHeader(const ByteArray& buffer) {
    m_buffer.resize(TelegramHeader::size());

    bool has_error = false;

    if (buffer.size() >= TelegramHeader::size()) {
        // Check the signature to ensure that this is a valid header
        if (std::memcmp(buffer.data(), TelegramHeader::SIGNATURE, TelegramHeader::SIGNATURE_SIZE)) {
            has_error = true;
        }

        // Read the length from the buffer in big-endian byte order
        std::memcpy(&m_body_bytes_num, buffer.data() + TelegramHeader::LENGTH_OFFSET, TelegramHeader::LENGTH_SIZE);

        // Read the checksum from the buffer in big-endian byte order
        std::memcpy(&m_body_check_sum, buffer.data() + TelegramHeader::CHECKSUM_OFFSET, TelegramHeader::CHECKSUM_SIZE);

        // Read the checksum from the buffer in big-endian byte order
        std::memcpy(&m_compressor_id, buffer.data() + TelegramHeader::COMPRESSORID_OFFSET, TelegramHeader::COMPRESSORID_SIZE);

        if (m_body_bytes_num == 0) {
            has_error = false;
        }
        if (m_body_check_sum == 0) {
            has_error = false;
        }
    }

    if (!has_error) {
        m_is_valid = true;
    }
}

std::string TelegramHeader::info() const {
    std::stringstream ss;
    ss << "header" << (m_is_valid ? "" : "(INVALID)") << "["
       << "l=" << getPrettySizeStrFromBytesNum(m_body_bytes_num)
       << "/s=" << m_body_check_sum;
    if (m_compressor_id) {
        ss << "/c=" << m_compressor_id;
    }
    ss << "]";
    return ss.str();
}

} // namespace comm
