/**
 * Overlays on Radix primitives (focus trap, Esc, aria-modal, return focus):
 * Dialog, Drawer (used by "Why?" explanations), Tooltip, and NavTabs.
 */
import * as RadixDialog from '@radix-ui/react-dialog';
import * as RadixTooltip from '@radix-ui/react-tooltip';
import { X } from 'lucide-react';
import { NavLink } from 'react-router-dom';
import type { ReactNode } from 'react';
import clsx from 'clsx';
import { Button } from './Layout';

export function Dialog({
  open,
  onOpenChange,
  title,
  description,
  children,
  footer,
  wide,
}: {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  title: ReactNode;
  description?: ReactNode;
  children: ReactNode;
  footer?: ReactNode;
  wide?: boolean;
}) {
  return (
    <RadixDialog.Root open={open} onOpenChange={onOpenChange}>
      <RadixDialog.Portal>
        <RadixDialog.Overlay className="vts-overlay" />
        <RadixDialog.Content className={clsx('vts-dialog', wide && 'vts-dialog--wide')} aria-describedby={description ? undefined : undefined}>
          <div className="vts-dialog__header row-between">
            <div className="stack-sm" style={{ gap: 2 }}>
              <RadixDialog.Title asChild>
                <h2 style={{ fontSize: 'var(--text-lg)' }}>{title}</h2>
              </RadixDialog.Title>
              {description ? (
                <RadixDialog.Description className="small muted">{description}</RadixDialog.Description>
              ) : (
                <RadixDialog.Description className="sr-only">{typeof title === 'string' ? title : 'Dialog'}</RadixDialog.Description>
              )}
            </div>
            <RadixDialog.Close asChild>
              <Button variant="ghost" iconOnly icon={<X size={16} />}>
                Close
              </Button>
            </RadixDialog.Close>
          </div>
          <div className="vts-dialog__body">{children}</div>
          {footer && <div className="vts-dialog__footer">{footer}</div>}
        </RadixDialog.Content>
      </RadixDialog.Portal>
    </RadixDialog.Root>
  );
}

/** Side drawer for explanations ("Why?") and record details. */
export function Drawer({
  open,
  onOpenChange,
  title,
  subtitle,
  children,
}: {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  title: ReactNode;
  subtitle?: ReactNode;
  children: ReactNode;
}) {
  return (
    <RadixDialog.Root open={open} onOpenChange={onOpenChange}>
      <RadixDialog.Portal>
        <RadixDialog.Overlay className="vts-overlay" />
        <RadixDialog.Content className="vts-drawer">
          <div className="vts-drawer__header">
            <div className="stack-sm" style={{ gap: 2, minWidth: 0 }}>
              <RadixDialog.Title asChild>
                <h2 style={{ fontSize: 'var(--text-lg)' }}>{title}</h2>
              </RadixDialog.Title>
              <RadixDialog.Description className="small muted">{subtitle ?? ' '}</RadixDialog.Description>
            </div>
            <RadixDialog.Close asChild>
              <Button variant="ghost" iconOnly icon={<X size={16} />}>
                Close
              </Button>
            </RadixDialog.Close>
          </div>
          <div className="vts-drawer__body">{children}</div>
        </RadixDialog.Content>
      </RadixDialog.Portal>
    </RadixDialog.Root>
  );
}

/** Supplementary hint on hover/focus. Never the only place critical information appears. */
export function Tooltip({ content, children, side }: { content: ReactNode; children: ReactNode; side?: 'top' | 'right' | 'bottom' | 'left' }) {
  return (
    <RadixTooltip.Root delayDuration={300}>
      <RadixTooltip.Trigger asChild>{children}</RadixTooltip.Trigger>
      <RadixTooltip.Portal>
        <RadixTooltip.Content className="vts-tooltip" sideOffset={6} side={side}>
          {content}
        </RadixTooltip.Content>
      </RadixTooltip.Portal>
    </RadixTooltip.Root>
  );
}

/** Route-based tabs (each tab is a link, so views are deep-linkable). */
export function NavTabs({ tabs, label }: { tabs: { to: string; label: ReactNode; end?: boolean }[]; label: string }) {
  return (
    <nav className="vts-tabs" aria-label={label}>
      {tabs.map((t) => (
        <NavLink key={t.to} to={t.to} end={t.end} className={({ isActive }) => clsx('vts-tab', isActive && 'active')}>
          {t.label}
        </NavLink>
      ))}
    </nav>
  );
}

/** In-page tabs (state, not routes). */
export function Tabs<T extends string>({
  value,
  onChange,
  tabs,
  label,
}: {
  value: T;
  onChange: (v: T) => void;
  tabs: { id: T; label: ReactNode }[];
  label: string;
}) {
  return (
    <div className="vts-tabs" role="tablist" aria-label={label}>
      {tabs.map((t) => (
        <button
          key={t.id}
          type="button"
          role="tab"
          className="vts-tab"
          aria-selected={value === t.id}
          onClick={() => onChange(t.id)}
        >
          {t.label}
        </button>
      ))}
    </div>
  );
}

export function Segmented<T extends string>({
  value,
  onChange,
  options,
  label,
}: {
  value: T;
  onChange: (v: T) => void;
  options: { id: T; label: string }[];
  label: string;
}) {
  return (
    <div className="vts-segmented" role="group" aria-label={label}>
      {options.map((o) => (
        <button key={o.id} type="button" aria-pressed={value === o.id} onClick={() => onChange(o.id)}>
          {o.label}
        </button>
      ))}
    </div>
  );
}
