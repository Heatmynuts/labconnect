export function formatCount(value: number, singular: string, plural = `${singular}s`) {
  return `${value} ${value > 1 ? plural : singular}`;
}
