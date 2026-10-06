import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import * as RadixTooltip from '@radix-ui/react-tooltip';
import { RouterProvider } from 'react-router-dom';
import { useState } from 'react';
import { ApiError } from '@/api/client';
import { LiveProvider } from '@/live/LiveProvider';
import { PrefsProvider } from './disclosure';
import { createAppRouter } from './router';

export function makeQueryClient() {
  return new QueryClient({
    defaultOptions: {
      queries: {
        staleTime: 15_000,
        refetchOnWindowFocus: true,
        retry: (count, error) => {
          if (error instanceof ApiError && (error.status === 404 || error.status === 400 || error.isRuntimeNotConnected)) return false;
          return count < 2;
        },
      },
    },
  });
}

export function App() {
  const [client] = useState(makeQueryClient);
  const [router] = useState(createAppRouter);
  return (
    <QueryClientProvider client={client}>
      <PrefsProvider>
        <LiveProvider>
          <RadixTooltip.Provider>
            <RouterProvider router={router} />
          </RadixTooltip.Provider>
        </LiveProvider>
      </PrefsProvider>
    </QueryClientProvider>
  );
}
