import { beforeEach, describe, expect, it } from "vitest";
import { isRateLimited, resetRateLimitState } from "../src/rateLimit";

beforeEach(() => {
  resetRateLimitState();
});

describe("isRateLimited", () => {
  it("allows requests under the limit and blocks over it within one window", () => {
    const now = 1_000_000;
    for (let i = 0; i < 5; i++) {
      expect(isRateLimited("1.2.3.4", now, 5, 60_000)).toBe(false);
    }
    expect(isRateLimited("1.2.3.4", now, 5, 60_000)).toBe(true);
  });

  it("resets a client's count once its window has elapsed", () => {
    const windowMs = 60_000;
    for (let i = 0; i < 5; i++) {
      isRateLimited("1.2.3.4", 0, 5, windowMs);
    }
    expect(isRateLimited("1.2.3.4", 0, 5, windowMs)).toBe(true);
    expect(isRateLimited("1.2.3.4", windowMs, 5, windowMs)).toBe(false);
  });

  it("does not reset an actively-limited client's count when the tracked-IP overflow sweep runs", () => {
    const now = 0;
    const windowMs = 60_000;
    // Push one client over its limit, still well within its window.
    for (let i = 0; i < 6; i++) {
      isRateLimited("attacker", now, 5, windowMs);
    }
    expect(isRateLimited("attacker", now, 5, windowMs)).toBe(true);

    // Flood with >10,000 distinct, still-active IPs to trigger the overflow
    // path. A naive "clear everything" implementation would wipe the
    // attacker's count here, handing it a fresh window early.
    for (let i = 0; i < 10_001; i++) {
      isRateLimited(`ip-${i}`, now, 5, windowMs);
    }

    expect(isRateLimited("attacker", now, 5, windowMs)).toBe(true);
  });
});
