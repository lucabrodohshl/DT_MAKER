/**
 * Registered domain plugins. Generic views never import plugin code directly;
 * they ask the registry for plugins matching a twin. Removing a plugin from this
 * list removes its view and nothing else.
 */
import type { TwinSummary } from '@/api/types';
import type { DomainPlugin } from './types';

const plugins: DomainPlugin[] = [];

/** Register a plugin (called from plugins/index.ts). */
export function registerPlugin(plugin: DomainPlugin): void {
  if (!plugins.some((p) => p.id === plugin.id)) plugins.push(plugin);
}

/** Plugins applicable to a twin. */
export function pluginsFor(twin: TwinSummary | null | undefined): DomainPlugin[] {
  if (!twin) return [];
  return plugins.filter((p) => {
    try {
      return p.matches(twin);
    } catch {
      return false;
    }
  });
}

/** All registered plugins (for the About panel). */
export function allPlugins(): readonly DomainPlugin[] {
  return plugins;
}
