import { useCallback, useEffect, useRef, useState } from 'react';
import type { Command, EngineEvent } from './protocol';

const BACKOFF_MIN_MS = 250;
const BACKOFF_MAX_MS = 5000;

export type ConnectionState = 'connecting' | 'open' | 'closed';

export interface Engine {
  connection: ConnectionState;
  send: (cmd: Command) => void;
}

// Tient la socket unique vers le moteur et la reconnecte en backoff
// exponentiel 250 ms -> 5 s. L'URL est relative : le port est choisi au
// demarrage du moteur, jamais ecrit en dur. Chaque trame recue est passee a
// onEvent.
export function useEngine(onEvent: (event: EngineEvent) => void): Engine {
  const [connection, setConnection] = useState<ConnectionState>('connecting');
  const socketRef = useRef<WebSocket | null>(null);
  const handlerRef = useRef(onEvent);
  handlerRef.current = onEvent;

  useEffect(() => {
    let cancelled = false;
    let backoff = BACKOFF_MIN_MS;
    let retry: ReturnType<typeof setTimeout> | undefined;

    const connect = () => {
      if (cancelled) return;
      setConnection('connecting');
      const socket = new WebSocket(`ws://${location.host}/ws`);
      socketRef.current = socket;

      socket.onopen = () => {
        backoff = BACKOFF_MIN_MS;
        setConnection('open');
      };

      socket.onmessage = (e) => {
        try {
          handlerRef.current(JSON.parse(e.data as string) as EngineEvent);
        } catch {
          console.error('[moteur] trame illisible', e.data);
        }
      };

      socket.onclose = () => {
        if (cancelled) return;
        socketRef.current = null;
        setConnection('closed');
        retry = setTimeout(connect, backoff);
        backoff = Math.min(backoff * 2, BACKOFF_MAX_MS);
      };
    };

    connect();

    return () => {
      cancelled = true;
      clearTimeout(retry);
      socketRef.current?.close();
      socketRef.current = null;
    };
  }, []);

  const send = useCallback((cmd: Command) => {
    const socket = socketRef.current;
    if (socket?.readyState === WebSocket.OPEN) socket.send(JSON.stringify(cmd));
  }, []);

  return { connection, send };
}
