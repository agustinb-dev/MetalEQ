/*
    Modal dialog used for preset naming/confirmation, mirroring the native
    juce::AlertWindow flows:
    * text prompt with default value, Enter = confirm, Escape = cancel
    * confirmation (OK/Cancel)
    * plain error message box (single OK)
*/

import { useEffect, useRef, useState } from 'react';

interface ModalProps {
  title: string;
  message: string;
  /** When set, a text input is shown pre-filled with this value. */
  initialText?: string;
  inputPlaceholder?: string;
  confirmLabel: string;
  cancelLabel?: string;
  /** Called with the (trimmed) input text when there is an input, else with undefined. */
  onConfirm: (text: string | undefined) => void;
  onClose: () => void;
}

export function Modal({
  title,
  message,
  initialText,
  inputPlaceholder,
  confirmLabel,
  cancelLabel = 'Cancel',
  onConfirm,
  onClose,
}: ModalProps) {
  const [text, setText] = useState(initialText ?? '');
  const inputRef = useRef<HTMLInputElement>(null);
  const boxRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (initialText !== undefined) {
      inputRef.current?.focus();
      inputRef.current?.select();
    } else {
      boxRef.current?.querySelector<HTMLButtonElement>('button.primary')?.focus();
    }
  }, [initialText]);

  const confirm = () => onConfirm(initialText !== undefined ? text.trim() : undefined);

  return (
    <div className="modal-backdrop" onPointerDown={(e) => e.target === e.currentTarget && onClose()}>
      <div
        className="modal"
        role="dialog"
        aria-modal="true"
        aria-label={title}
        ref={boxRef}
        onKeyDown={(e) => {
          if (e.key === 'Enter') {
            e.preventDefault();
            confirm();
          } else if (e.key === 'Escape') {
            e.preventDefault();
            onClose();
          }
        }}
      >
        <div className="modal-title">{title}</div>
        <div className="modal-message">{message}</div>
        {initialText !== undefined && (
          <input
            ref={inputRef}
            className="modal-input"
            value={text}
            placeholder={inputPlaceholder}
            onChange={(e) => setText(e.target.value)}
            maxLength={64}
          />
        )}
        <div className="modal-actions">
          {cancelLabel !== '' && (
            <button type="button" className="btn" onClick={onClose}>
              {cancelLabel}
            </button>
          )}
          <button
            type="button"
            className="btn primary"
            onClick={() => {
              confirm();
              // For message-only boxes (no input) OK also closes.
              if (initialText === undefined) onClose();
            }}
          >
            {confirmLabel}
          </button>
        </div>
      </div>
    </div>
  );
}
