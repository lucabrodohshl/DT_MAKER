import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import '@/design/tokens.css';
import '@/design/base.css';
import '@/design/components.css';
import '@/app/shell.css';
import '@/plugins';
import { App } from '@/app/App';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
