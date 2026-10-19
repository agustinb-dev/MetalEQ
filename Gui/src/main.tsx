/*
    Entry point for the MetalEQ WebView front-end.

    Inside the plugin the JUCE backend is injected before any module code
    runs (window.__JUCE__), so the bridge selection in App.tsx sees it.
    In a plain browser there is no backend and the app runs on MockBridge.

    No StrictMode: its dev-only double effect invocation would call
    bridge.init() twice (duplicate tick listeners).
*/

import { createRoot } from 'react-dom/client';
import { App } from './App';
import './styles.css';

const container = document.getElementById('root');
if (container == null) throw new Error('MetalEQ UI: #root element missing');

createRoot(container).render(<App />);
