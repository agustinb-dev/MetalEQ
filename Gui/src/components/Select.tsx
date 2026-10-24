/*
    Dark <select> styled like the native juce::ComboBox.
    Value is the option key (string); caller maps to/from plain values.
*/

export interface SelectOption {
  value: string;
  label: string;
  disabled?: boolean;
  /** Optional optgroup label (e.g. "Genres", "Artists", "Producers"). */
  group?: string;
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
  const groups: { name: string | null; options: SelectOption[] }[] = [];
  for (const option of options) {
    const name = option.group ?? null;
    const last = groups[groups.length - 1];
    if (last && last.name === name) last.options.push(option);
    else groups.push({ name, options: [option] });
  }

  return (
    <select
      className={`select ${className ?? ''}`}
      title={tip}
      aria-label={ariaLabel}
      value={value}
      onChange={(e) => onChange(e.target.value)}
    >
      {groups.map((group) =>
        group.name == null ? (
          group.options.map((o) => (
            <option key={o.value} value={o.value} disabled={o.disabled}>
              {o.label}
            </option>
          ))
        ) : (
          <optgroup key={group.name} label={group.name}>
            {group.options.map((o) => (
              <option key={o.value} value={o.value} disabled={o.disabled}>
                {o.label}
              </option>
            ))}
          </optgroup>
        ),
      )}
    </select>
  );
}
