/**
 * Accessible data table on TanStack Table: sortable headers (aria-sort), optional
 * row selection, stable column layout, and row virtualisation for long lists so
 * thousands of rows never become thousands of DOM nodes.
 *
 * Live updates never reorder rows on their own: sorting changes only when the
 * user changes it, and selection is keyed by row id, not index.
 */
import {
  flexRender,
  getCoreRowModel,
  getSortedRowModel,
  useReactTable,
  type ColumnDef,
  type SortingState,
} from '@tanstack/react-table';
import { useVirtualizer } from '@tanstack/react-virtual';
import { ArrowDown, ArrowUp, ArrowUpDown } from 'lucide-react';
import { useRef, useState, type ReactNode } from 'react';
import clsx from 'clsx';

export interface DataTableProps<T> {
  data: T[];
  columns: ColumnDef<T, unknown>[];
  getRowId: (row: T) => string;
  onRowClick?: (row: T) => void;
  selectedId?: string | null;
  caption: string;
  /** Rows beyond this count are virtualised (default 200). */
  virtualizeAbove?: number;
  maxHeight?: number;
  initialSorting?: SortingState;
  empty?: ReactNode;
}

export function DataTable<T>({
  data,
  columns,
  getRowId,
  onRowClick,
  selectedId,
  caption,
  virtualizeAbove = 200,
  maxHeight = 560,
  initialSorting = [],
  empty,
}: DataTableProps<T>) {
  const [sorting, setSorting] = useState<SortingState>(initialSorting);
  const table = useReactTable({
    data,
    columns,
    state: { sorting },
    onSortingChange: setSorting,
    getRowId: (row) => getRowId(row),
    getCoreRowModel: getCoreRowModel(),
    getSortedRowModel: getSortedRowModel(),
  });
  const rows = table.getRowModel().rows;
  const parentRef = useRef<HTMLDivElement>(null);
  const virtual = rows.length > virtualizeAbove;
  const virtualizer = useVirtualizer({
    count: rows.length,
    getScrollElement: () => parentRef.current,
    estimateSize: () => 37,
    overscan: 12,
    enabled: virtual,
  });
  const items = virtual ? virtualizer.getVirtualItems() : null;
  const padTop = items && items.length > 0 ? items[0]!.start : 0;
  const padBottom = items && items.length > 0 ? virtualizer.getTotalSize() - items[items.length - 1]!.end : 0;
  const visible = items ? items.map((i) => rows[i.index]!) : rows;

  if (rows.length === 0 && empty) return <>{empty}</>;

  return (
    <div className="vts-table-wrap" ref={parentRef} style={{ maxHeight }}>
      <table className="vts-table">
        <caption className="sr-only">{caption}</caption>
        <thead>
          {table.getHeaderGroups().map((hg) => (
            <tr key={hg.id}>
              {hg.headers.map((h) => {
                const sorted = h.column.getIsSorted();
                const canSort = h.column.getCanSort();
                const meta = h.column.columnDef.meta as { align?: 'right'; width?: number } | undefined;
                return (
                  <th
                    key={h.id}
                    scope="col"
                    className={clsx(meta?.align === 'right' && 'num')}
                    style={{ width: meta?.width }}
                    aria-sort={sorted === 'asc' ? 'ascending' : sorted === 'desc' ? 'descending' : canSort ? 'none' : undefined}
                  >
                    {h.isPlaceholder ? null : canSort ? (
                      <button type="button" onClick={h.column.getToggleSortingHandler()}>
                        {flexRender(h.column.columnDef.header, h.getContext())}
                        {sorted === 'asc' ? <ArrowUp size={12} /> : sorted === 'desc' ? <ArrowDown size={12} /> : <ArrowUpDown size={12} opacity={0.4} />}
                      </button>
                    ) : (
                      flexRender(h.column.columnDef.header, h.getContext())
                    )}
                  </th>
                );
              })}
            </tr>
          ))}
        </thead>
        <tbody>
          {padTop > 0 && (
            <tr aria-hidden="true">
              <td colSpan={columns.length} style={{ height: padTop, padding: 0, border: 0 }} />
            </tr>
          )}
          {visible.map((row) => (
            <tr
              key={row.id}
              className={clsx(onRowClick && 'is-clickable')}
              aria-selected={selectedId !== undefined ? row.id === selectedId : undefined}
              onClick={onRowClick ? () => onRowClick(row.original) : undefined}
              onKeyDown={
                onRowClick
                  ? (e) => {
                      if (e.key === 'Enter' || e.key === ' ') {
                        e.preventDefault();
                        onRowClick(row.original);
                      }
                    }
                  : undefined
              }
              tabIndex={onRowClick ? 0 : undefined}
            >
              {row.getVisibleCells().map((cell) => {
                const meta = cell.column.columnDef.meta as { align?: 'right' } | undefined;
                return (
                  <td key={cell.id} className={clsx(meta?.align === 'right' && 'num')}>
                    {flexRender(cell.column.columnDef.cell, cell.getContext())}
                  </td>
                );
              })}
            </tr>
          ))}
          {padBottom > 0 && (
            <tr aria-hidden="true">
              <td colSpan={columns.length} style={{ height: padBottom, padding: 0, border: 0 }} />
            </tr>
          )}
        </tbody>
      </table>
    </div>
  );
}
