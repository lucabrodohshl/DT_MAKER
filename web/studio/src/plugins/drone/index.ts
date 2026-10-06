/**
 * Indoor-drone domain plugin: mission map (ground truth vs twin knowledge),
 * planner candidates, operational panel and ledger-backed timeline.
 * Matches twins whose presentation metadata says `"plugin": "drone"`.
 */
import { registerPlugin } from '@/plugins/registry';
import { DroneMissionView } from './DroneMissionView';

registerPlugin({
  id: 'drone',
  title: 'Mission map',
  description: 'Physical world vs digital-twin knowledge, routes, replanning and the mission timeline of an inspection drone.',
  matches: (twin) => twin.presentation?.plugin === 'drone',
  Component: DroneMissionView,
});
