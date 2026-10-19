/*
    Dark <select> styled like the native juce::ComboBox.
    Value is the option key (string); caller maps to/from plain values.
*/

export interface SelectOption {
  value: string;
  label: string;
  disabled?: boolean;
}

interface SelectProps {
  value: string;
  options: SelectOption[];
  onChange: (value: string) => void;
  tip?: string;
  ariaLabel?: string;
  className?: string;
}

export function Select({ value, options, onChange, tip, ariaLabel, className }: SelectProps) {
  return (
    <select
      className={`select ${className ?? ''}`}
      title={tip}
      aria-label={ariaLabel}
      value={value}
      onChange={(e) => onChange(e.target.value)}
    >
      {options.map((o) => (
        <option key={o.value} value={o.value} disabled={o.disabled}>
          {o.label}
        </option>
      ))}
    </select>
  );
}
