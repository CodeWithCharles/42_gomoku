const SCORE_WIN = 1_000_000;
const SCORE_MATE = 999_000;
const SCORE_DECISIVE = 500_000;

const NUMBER = new Intl.NumberFormat('fr-FR');

// Met en forme un entier a la francaise, espaces fines entre les milliers.
export function count(n: number): string {
  return NUMBER.format(n);
}

// Convertit des millisecondes en secondes a deux decimales, virgule comprise.
export function secs(ms: number): string {
  return (ms / 1000).toFixed(2).replace('.', ',');
}

// Traduit un score moteur : gain ou perte prouves (en demi-coups), position
// decisive selon l'evaluation, ou evaluation ordinaire.
export function scoreLabel(score: number): string {
  if (score >= SCORE_MATE) return `gain forcé en ${SCORE_WIN - score}`;
  if (score <= -SCORE_MATE) return `perte forcée en ${SCORE_WIN + score}`;
  if (Math.abs(score) >= SCORE_DECISIVE) return 'décisif';
  return count(score);
}

// Classe le score : favorable, defavorable ou neutre pour le camp qui cherche.
export function scoreTone(score: number): 'win' | 'loss' | 'neutral' {
  if (score >= SCORE_DECISIVE) return 'win';
  if (score <= -SCORE_DECISIVE) return 'loss';
  return 'neutral';
}
