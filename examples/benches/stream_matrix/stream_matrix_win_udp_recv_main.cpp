/*
 * Copyright 2026 Aethernet Inc.
 * Standalone entry for Windows UDP WinSocket recv scenarios.
 */

#include "stream_matrix/stream_matrix_win_udp_recv_test.h"

#if defined(_WIN32)
int main() {
  RunStreamMatrixWinUdpRecvTests();
  return stream_matrix_win_udp_recv_test_failures != 0 ? 1 : 0;
}
#else
int main() { return 0; }
#endif
