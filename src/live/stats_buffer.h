#pragma once

// Pinned PC multiplayer executable: DDL_AssociateBuffer receives 0xA000.
inline constexpr int STATS_BUFFER_SIZE = 0xA000;
inline constexpr int MODIFIED_STATS_BYTE_SIZE = STATS_BUFFER_SIZE / 8;
