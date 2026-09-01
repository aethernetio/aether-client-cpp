/*
 * Copyright 2026 Aethernet Inc.
 */

#include <unity.h>

void setUp() {}
void tearDown() {}

int test_product_probe_select();
int test_product_probe_protocol();

int main() {
  if (auto const rc = test_product_probe_select(); rc != 0) {
    return rc;
  }
  return test_product_probe_protocol();
}
