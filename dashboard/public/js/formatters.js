export function finite(value) {
  if (value === null || value === undefined || value === '') return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}
export function formatG(value) {
  const n = finite(value); if (n === null) return '—';
  return Math.abs(n) < 0.1 ? n.toFixed(4) : n.toFixed(3);
}
export function formatCrest(value) { const n = finite(value); return n === null ? '—' : n.toFixed(2); }
export function formatPercent(value) { const n = finite(value); return n === null ? '—' : `${Math.round(n * 100)}%`; }
export function formatTime(timestamp) {
  if (!Number.isFinite(timestamp)) return '—';
  const d = new Date(timestamp); return `${d.toLocaleTimeString([], { hour12:false })}.${String(d.getMilliseconds()).padStart(3,'0')}`;
}
export function formatAge(ms) {
  if (!Number.isFinite(ms)) return 'No data';
  if (ms < 1000) return 'Updated now';
  return `Updated ${(ms / 1000).toFixed(ms < 10000 ? 1 : 0)} s ago`;
}
export function stateLabel(value) { return String(value || 'UNKNOWN').replace('_',' '); }
export function csvCell(value) { const s = String(value ?? ''); return /[",\n]/.test(s) ? `"${s.replaceAll('"','""')}"` : s; }
