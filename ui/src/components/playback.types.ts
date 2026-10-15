import type { Color, OpeningOption, Stats } from '../protocol';

export type PlaybackState = 'playing' | 'paused' | 'searching';

export interface PlaybackBarProps {
  aiOnly: boolean;
  playback: PlaybackState;
  position: number | null;
  length: number;
  canBack: boolean;
  canForward: boolean;
  forwardSteps: boolean;
  onFirst: () => void;
  onBack: () => void;
  onTogglePlay: () => void;
  onForward: () => void;
  onLast: () => void;
}

export type TimelineItem =
  | { kind: 'move'; number: number; idx: number; player: Color; captured: number; byAi: boolean }
  | { kind: 'choice'; actor: Color; choice: OpeningOption; swapped: boolean; byAi: boolean };

export interface MoveHistoryProps {
  items: TimelineItem[];
  cursor: number | null;
  onSelect: (position: number) => void;
}

export interface PastViewProps {
  position: number;
  length: number;
  onGoLive: () => void;
}

export interface AnalysisHeaderProps {
  source: 'live' | 'move' | 'none';
  position: number | null;
  stats: Stats | null;
}
