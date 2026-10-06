/** Test helpers: render a page inside the app providers with a memory router and an MSW server. */
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import * as RadixTooltip from '@radix-ui/react-tooltip';
import { render } from '@testing-library/react';
import { setupServer } from 'msw/node';
import type { ReactNode } from 'react';
import { createMemoryRouter, RouterProvider, type RouteObject } from 'react-router-dom';
import { PrefsProvider } from '@/app/disclosure';

export const server = setupServer();

export function renderRoute(routes: RouteObject[], initial: string) {
  const client = new QueryClient({ defaultOptions: { queries: { retry: false, staleTime: 0 }, mutations: { retry: false } } });
  const router = createMemoryRouter(routes, { initialEntries: [initial] });
  return render(
    <QueryClientProvider client={client}>
      <PrefsProvider>
        <RadixTooltip.Provider>
          <RouterProvider router={router} />
        </RadixTooltip.Provider>
      </PrefsProvider>
    </QueryClientProvider>,
  );
}

export function renderWithProviders(ui: ReactNode) {
  return renderRoute([{ path: '/', element: ui }], '/');
}
