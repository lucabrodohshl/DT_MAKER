/** Icon of a Blueprint or template by its stored name (presentation only; unknown names fall back to Boxes). */
import { Bot, Boxes, Cpu, Drone, Factory, Fan, Gauge, Thermometer, type LucideIcon } from 'lucide-react';

const ICONS: Record<string, LucideIcon> = {
  boxes: Boxes,
  drone: Drone,
  pump: Fan,
  fan: Fan,
  thermometer: Thermometer,
  'thermal-chamber': Thermometer,
  factory: Factory,
  cpu: Cpu,
  robot: Bot,
  gauge: Gauge,
};

export function iconFor(name: string | null | undefined, size = 20) {
  const Icon = (name && ICONS[name]) || Boxes;
  return <Icon size={size} aria-hidden="true" />;
}
