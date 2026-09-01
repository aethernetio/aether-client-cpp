/*
 * Copyright 2026 Aethernet Inc.
 */

#include <unity.h>

void setUp() {}
void tearDown() {}

int test_product_probe_select();
int test_product_probe_protocol();
int test_power_factor();

int main() {
  if (auto const rc = test_product_probe_select(); rc != 0) {
    return rc;
  }
  if (auto const rc = test_product_probe_protocol(); rc != 0) {
    return rc;
  }
  return test_power_factor();
}
