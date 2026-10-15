import type { Color, OpeningOption } from '../protocol';

// Nom francais d'une couleur quand elle designe un joueur ("Noir", "Blanc").
export function colorName(color: Color): string {
  return color === 'black' ? 'Noir' : 'Blanc';
}

// Modificateur de classe qui choisit la pastille de pierre d'une couleur.
export function stoneModifier(color: Color): string {
  return color === 'black' ? 'black' : 'white';
}

// Fin de "<joueur> choisit ..." pour une option d'ouverture ; ne compile plus
// si l'union change.
export function optionLabel(option: OpeningOption): string {
  switch (option) {
    case 'black':
      return 'Noir';
    case 'white':
      return 'Blanc';
    case 'place2':
      return 'de poser deux pierres';
    default: {
      const unhandled: never = option;
      return unhandled;
    }
  }
}
