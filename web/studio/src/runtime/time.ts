/** Exact rendering of logical time: integer ticks -> decimal text in model units (no floating point). */
export function ticksToText(ticks: number, ticksPerUnit: number): string {
  if (!Number.isInteger(ticks) || !Number.isInteger(ticksPerUnit) || ticksPerUnit <= 0) return String(ticks);
  const sign = ticks < 0 ? '-' : '';
  const abs = Math.abs(ticks);
  const whole = Math.floor(abs / ticksPerUnit);
  const rem = abs % ticksPerUnit;
  if (rem === 0) return `${sign}${whole}`;
  const width = String(ticksPerUnit).length - 1;
  const frac = String(rem).padStart(width, '0').replace(/0+$/, '');
  return `${sign}${whole}.${frac}`;
}
