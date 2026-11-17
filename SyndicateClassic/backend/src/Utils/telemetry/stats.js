// Small statistics helpers shared by the analyses. Every rate comes with its sample size and a
// 95% Wilson interval, so "3 of 4" is never presented as confidently as "300 of 400".

export function wilson(k, n, z = 1.96) {
  if (!n) return { rate: null, low: null, high: null, n: 0 };
  const p = k / n, d = 1 + z * z / n;
  const centre = (p + z * z / (2 * n)) / d;
  const half = (z * Math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))) / d;
  return { rate: round(p), low: round(Math.max(0, centre - half)), high: round(Math.min(1, centre + half)), n };
}

export function mean(values) { const v = values.filter(Number.isFinite); return v.length ? round(v.reduce((a, b) => a + b, 0) / v.length) : null; }

export function median(values) {
  const v = values.filter(Number.isFinite).sort((a, b) => a - b);
  if (!v.length) return null;
  const m = Math.floor(v.length / 2);
  return round(v.length % 2 ? v[m] : (v[m - 1] + v[m]) / 2);
}

export function round(x, digits = 4) { return Number.isFinite(x) ? Math.round(x * 10 ** digits) / 10 ** digits : null; }

// Confidence label from the sample size and the interval width.
export function confidence(n, interval = null) {
  if (n < 5) return 'insufficient';
  const width = interval && interval.high != null ? interval.high - interval.low : null;
  if (n >= 50 && (width == null || width < 0.2)) return 'high';
  if (n >= 15) return 'medium';
  return 'low';
}

// Two-proportion z test: is rate B different from rate A? Returns z and a two-sided p value.
export function compareRates(kA, nA, kB, nB) {
  if (!nA || !nB) return { z: null, p: null };
  const pA = kA / nA, pB = kB / nB, p = (kA + kB) / (nA + nB);
  const se = Math.sqrt(p * (1 - p) * (1 / nA + 1 / nB));
  if (!se) return { z: 0, p: 1 };
  const z = (pB - pA) / se;
  return { z: round(z), p: round(2 * (1 - normalCdf(Math.abs(z)))) };
}

function normalCdf(x) {
  // Abramowitz-Stegun 7.1.26
  const t = 1 / (1 + 0.3275911 * x / Math.SQRT2);
  const y = 1 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496735) * t + 0.254829592) * t * Math.exp(-x * x / 2);
  return 0.5 * (1 + y);
}

export function distance2d(a, b) { return Array.isArray(a) && Array.isArray(b) ? Math.hypot(a[0] - b[0], a[1] - b[1]) : null; }
