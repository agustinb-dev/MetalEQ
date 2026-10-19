/*
    Styled toggle button used for shape switches, band ON, LP Enabled,
    Bypass and A/B. Semantics match juce::ToggleButton + ButtonAttachment:
    click toggles, value round-trips through setParameter.
*/

interface ToggleProps {
  label: string;
  checked: boolean;
  onChange: (v: boolean) => void;
  tip?: string;
  disabled?: boolean;
  variant?: 'default' | 'accent';
}

export function Toggle({ label, checked, onChange, tip, disabled, variant = 'default' }: ToggleProps) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={label}
      title={tip ?? label}
      disabled={disabled}
      className={`toggle ${checked ? 'toggle-on' : ''} ${variant === 'accent' ? 'toggle-accent' : ''}`}
      onClick={() => onChange(!checked)}
    >
      <span className="toggle-led" />
      <span className="toggle-label">{label}</span>
    </button>
  );
}
