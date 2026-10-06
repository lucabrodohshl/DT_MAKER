/**
 * Per-viewer presentation preferences: disclosure depth (Operations vs Engineering)
 * and colour theme.
 *
 * Disclosure only controls how much formal detail is expanded by default (hashes,
 * formulas, checker identities). It is NOT an access-control mechanism: every
 * route stays reachable and every security decision belongs to the server.
 */
import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from 'react';

export type Disclosure = 'operations' | 'engineering';
export type Theme = 'light' | 'dark' | 'system';

interface Prefs {
  disclosure: Disclosure;
  engineering: boolean;
  setDisclosure: (d: Disclosure) => void;
  theme: Theme;
  setTheme: (t: Theme) => void;
}

const PrefsContext = createContext<Prefs>({
  disclosure: 'engineering',
  engineering: true,
  setDisclosure: () => undefined,
  theme: 'system',
  setTheme: () => undefined,
});

function read<T extends string>(key: string, allowed: readonly T[], fallback: T): T {
  try {
    const v = localStorage.getItem(key) as T | null;
    return v && allowed.includes(v) ? v : fallback;
  } catch {
    return fallback;
  }
}

function write(key: string, value: string) {
  try {
    localStorage.setItem(key, value);
  } catch {
    // preference only
  }
}

export function PrefsProvider({ children }: { children: ReactNode }) {
  const [disclosure, setD] = useState<Disclosure>(() => read('vts.disclosure', ['operations', 'engineering'] as const, 'engineering'));
  const [theme, setT] = useState<Theme>(() => read('vts.theme', ['light', 'dark', 'system'] as const, 'system'));

  useEffect(() => {
    const apply = () => {
      const dark = theme === 'dark' || (theme === 'system' && window.matchMedia?.('(prefers-color-scheme: dark)').matches);
      document.documentElement.dataset.theme = dark ? 'dark' : 'light';
    };
    apply();
    const mq = window.matchMedia?.('(prefers-color-scheme: dark)');
    mq?.addEventListener?.('change', apply);
    return () => mq?.removeEventListener?.('change', apply);
  }, [theme]);

  const setDisclosure = useCallback((d: Disclosure) => {
    setD(d);
    write('vts.disclosure', d);
  }, []);
  const setTheme = useCallback((t: Theme) => {
    setT(t);
    write('vts.theme', t);
  }, []);

  const value = useMemo(
    () => ({ disclosure, engineering: disclosure === 'engineering', setDisclosure, theme, setTheme }),
    [disclosure, setDisclosure, theme, setTheme],
  );
  return <PrefsContext.Provider value={value}>{children}</PrefsContext.Provider>;
}

export const useDisclosure = () => useContext(PrefsContext);
