/**
 * Layout primitives: buttons, panels, page headers, callouts, empty states and
 * the mode banners that make SIMULATION / REPLAY / PAUSED impossible to miss.
 */
import { AlertOctagon, AlertTriangle, CheckCircle2, FlaskConical, History, Info, Loader2, PauseCircle, ShieldCheck } from 'lucide-react';
import clsx from 'clsx';
import { forwardRef, type ButtonHTMLAttributes, type ReactNode } from 'react';
import type { Tone } from '@/api/types';
import { useTwinScope } from '@/app/twinScope';

export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: 'primary' | 'secondary' | 'ghost' | 'danger';
  size?: 'sm' | 'md';
  icon?: ReactNode;
  loading?: boolean;
  /** Icon-only button: `children` is used as the accessible label. */
  iconOnly?: boolean;
}

/** Plain text of children made of strings and numbers (e.g. `Delete {id}`), for icon-only labels. */
function textOf(children: ReactNode): string | undefined {
  if (typeof children === 'string' || typeof children === 'number') return String(children);
  if (Array.isArray(children) && children.length > 0 && children.every((c) => typeof c === 'string' || typeof c === 'number')) return children.join('');
  return undefined;
}

export const Button = forwardRef<HTMLButtonElement, ButtonProps>(function Button(
  { variant = 'secondary', size = 'md', icon, loading, iconOnly, children, className, disabled, type = 'button', ...rest },
  ref,
) {
  const label = iconOnly ? textOf(children) : undefined;
  return (
    <button
      ref={ref}
      type={type}
      className={clsx(
        'vts-btn',
        variant !== 'secondary' && `vts-btn--${variant}`,
        size === 'sm' && 'vts-btn--sm',
        iconOnly && 'vts-btn--icon',
        className,
      )}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      aria-label={label ?? rest['aria-label']}
      title={label ?? rest.title}
      {...rest}
    >
      {loading ? <Loader2 size={14} className="vts-spin" aria-hidden="true" /> : icon}
      {!iconOnly && children}
    </button>
  );
});

export function Panel({
  title,
  subtitle,
  actions,
  children,
  flush,
  footer,
  id,
  className,
}: {
  title?: ReactNode;
  subtitle?: ReactNode;
  actions?: ReactNode;
  children: ReactNode;
  flush?: boolean;
  footer?: ReactNode;
  id?: string;
  className?: string;
}) {
  const headingId = id ? `${id}-title` : undefined;
  return (
    <section className={clsx('vts-panel', className)} aria-labelledby={title ? headingId : undefined} id={id}>
      {(title || actions) && (
        <header className="vts-panel__header">
          <div className="stack-sm" style={{ gap: 2, minWidth: 0 }}>
            {title && (
              <h2 className="vts-panel__title" id={headingId}>
                {title}
              </h2>
            )}
            {subtitle && <span className="vts-panel__subtitle">{subtitle}</span>}
          </div>
          {actions && <div className="row-wrap">{actions}</div>}
        </header>
      )}
      <div className={clsx('vts-panel__body', flush && 'vts-panel__body--flush')}>{children}</div>
      {footer && <footer className="vts-panel__footer">{footer}</footer>}
    </section>
  );
}

export function PageHeader({
  eyebrow,
  title,
  meta,
  actions,
  children,
}: {
  eyebrow?: ReactNode;
  title: ReactNode;
  meta?: ReactNode;
  actions?: ReactNode;
  children?: ReactNode;
}) {
  // Inside a twin workspace the workspace header already shows the breadcrumbs.
  const inTwin = useTwinScope() !== null;
  return (
    <header className="vts-page-header">
      <div className="vts-page-header__titles">
        {eyebrow && !inTwin && <span className="vts-page-header__eyebrow">{eyebrow}</span>}
        <h1>{title}</h1>
        {meta && <div className="vts-page-header__meta">{meta}</div>}
        {children}
      </div>
      {actions && <div className="vts-page-header__actions">{actions}</div>}
    </header>
  );
}

const CALLOUT_ICON: Record<Tone, typeof Info> = {
  info: Info,
  ok: CheckCircle2,
  warning: AlertTriangle,
  critical: AlertOctagon,
  neutral: Info,
  formal: ShieldCheck,
};

export function Callout({ tone = 'info', title, children, action }: { tone?: Tone; title?: ReactNode; children?: ReactNode; action?: ReactNode }) {
  const Icon = CALLOUT_ICON[tone];
  return (
    <div className={clsx('vts-callout', tone !== 'info' && `vts-callout--${tone}`)} role={tone === 'critical' ? 'alert' : 'status'}>
      <Icon size={16} aria-hidden="true" />
      <div className="grow">
        {title && <div className="vts-callout__title">{title}</div>}
        {children && <div>{children}</div>}
      </div>
      {action}
    </div>
  );
}

export function EmptyState({
  icon,
  title,
  children,
  action,
  compact,
}: {
  icon?: ReactNode;
  title: ReactNode;
  children?: ReactNode;
  action?: ReactNode;
  compact?: boolean;
}) {
  return (
    <div className={clsx('vts-state', compact && 'vts-state--compact')}>
      {icon && <div className="vts-state__icon">{icon}</div>}
      <div className="vts-state__title">{title}</div>
      {children && <div className="vts-state__body">{children}</div>}
      {action}
    </div>
  );
}

export function ModeBanner({ mode, children, actions }: { mode: 'simulation' | 'replay' | 'paused'; children: ReactNode; actions?: ReactNode }) {
  const Icon = mode === 'simulation' ? FlaskConical : mode === 'replay' ? History : PauseCircle;
  const label = mode === 'simulation' ? 'SIMULATION' : mode === 'replay' ? 'REPLAY MODE' : 'LIVE UPDATES PAUSED';
  return (
    <div className={`vts-mode-banner vts-mode-banner--${mode}`} role="status">
      <Icon size={16} aria-hidden="true" />
      <span>{label}</span>
      <span style={{ fontWeight: 400 }} className="grow">
        {children}
      </span>
      {actions}
    </div>
  );
}

export function Skeleton({ lines = 3 }: { lines?: number }) {
  return (
    <div className="stack-sm" aria-hidden="true">
      {Array.from({ length: lines }, (_, i) => (
        <div key={i} className="vts-skeleton" style={{ width: `${90 - i * 12}%` }} />
      ))}
    </div>
  );
}
