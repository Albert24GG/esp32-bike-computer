#include "hal/gpio_types.h"
#include "soc/lp_timer_struct.h"
#include "ulp_lp_core.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_interrupts.h"
#include "ulp_lp_core_utils.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdbool.h>

#include "constants.hpp"


volatile uint64_t shared_periods_buf[SHARED_PERIODS_BUF_SIZE] = {0};
volatile uint32_t shared_periods_buf_len = 0;
volatile uint32_t is_first_wakeup = 1;
volatile uint32_t entered_inactive_state = 0;

static uint64_t last_wheel_counter = 0;

struct CircularBuffer {
  static constexpr uint32_t capacity = LP_CIRCULAR_BUF_SIZE;
  uint64_t buffer[LP_CIRCULAR_BUF_SIZE] = {};
  uint32_t head = 0;
  uint32_t tail = 0;
  uint32_t len = 0;

  void push(uint64_t value) {
    if (len >= capacity) {
      return;
    }

    buffer[tail] = value;
    tail = (tail + 1) % capacity;
    ++len;
  }

  /*
   * @brief Pops the first n elements from the buffer and writes them to out_buf
   * @param n The number of elements to pop
   * @param out_buf The buffer to write the popped elements to. Must be at least
   * of size n.
   * @return The number of elements actually popped (which may be less than n if
   * the buffer doesn't contain enough elements)
   */
  uint32_t pop_first_n(uint32_t n, uint64_t *out_buf) {
    if (n == 0 || len == 0) {
      return 0;
    }

    const uint32_t to_pop = std::min(n, len);
    const uint32_t space_to_end = capacity - head;
    if (to_pop <= space_to_end) {
      // No wrap around
      memcpy(out_buf, buffer + head, to_pop * sizeof(uint64_t));
    } else {
      // Wrap around
      memcpy(out_buf, buffer + head, space_to_end * sizeof(uint64_t));
      memcpy(out_buf + space_to_end, buffer,
             (to_pop - space_to_end) * sizeof(uint64_t));
    }

    head = (head + to_pop) % capacity;
    len -= to_pop;

    return to_pop;
  }
};

static CircularBuffer circular_periods_buf{};

static uint32_t inactivity_cnt = 0;
static bool notify_hp = false;

static uint64_t read_counter(size_t idx) {
  LP_TIMER.update.update = 1;

  while (LP_TIMER.update.update) {
    // Wait for the update to complete
  }

  uint64_t lo = LP_TIMER.counter[idx].lo.counter_lo;
  uint64_t hi = LP_TIMER.counter[idx].hi.counter_hi;
  uint64_t counter = (hi << 32) | lo;
  return counter;
}

static void update_wheel_periods_buf() {
  const uint64_t cur_counter = read_counter(0);

  const uint64_t cur_wheel_period = [&]() {
    if (cur_counter < last_wheel_counter) {
      // Handle counter overflow
      return (std::numeric_limits<uint64_t>::max() - last_wheel_counter) +
             cur_counter + 1;
    } else {
      return cur_counter - last_wheel_counter;
    }
  }();

  last_wheel_counter = cur_counter;

  if (is_first_wakeup == 1) {
    is_first_wakeup = 0;
  } else if (cur_wheel_period) {
    circular_periods_buf.push(cur_wheel_period);
  }
}

static void timer_handler() {
  ulp_lp_core_lp_timer_intr_clear();

  if (circular_periods_buf.len > 0) {
    if (shared_periods_buf_len == 0) {
      const uint32_t buf_len = circular_periods_buf.pop_first_n(
          SHARED_PERIODS_BUF_SIZE, const_cast<uint64_t *>(shared_periods_buf));

      shared_periods_buf_len = buf_len;
    }

    notify_hp = true;
    inactivity_cnt = 0;
    return;
  }

  ++inactivity_cnt;
  if (inactivity_cnt >= LP_WAKEUPS_BEFORE_TIMEOUT) {
    inactivity_cnt = 0;
    notify_hp = true;
  }
}

static void gpio_handler() {
  ulp_lp_core_gpio_clear_intr_status();

  update_wheel_periods_buf();

  if (circular_periods_buf.len >= SHARED_PERIODS_BUF_SIZE) {
    if (shared_periods_buf_len == 0) {
      const uint32_t popped = circular_periods_buf.pop_first_n(
          SHARED_PERIODS_BUF_SIZE, const_cast<uint64_t *>(shared_periods_buf));

      shared_periods_buf_len = popped;
    }

    notify_hp = true;
  }

  if (entered_inactive_state == 1) {
    notify_hp = true;
  }

  inactivity_cnt = 0;
}

int main(void) {

  const auto wakeup_cause = ulp_lp_core_get_wakeup_cause();

  if (wakeup_cause & ULP_LP_CORE_WAKEUP_SOURCE_LP_IO) {
    gpio_handler();
  } else if (wakeup_cause & ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER) {
    timer_handler();
  }

  if (notify_hp) {
    notify_hp = false;
    ulp_lp_core_wakeup_main_processor();
  }

  return 0;
}