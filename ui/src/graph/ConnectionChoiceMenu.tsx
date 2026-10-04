// The "how should stereo become mono?" chooser (wiki/plans/StereoChannels.md
// §3). Shown when a cable drop would throw information away — a stereo
// signal into a port that is genuinely one signal. Nothing is inserted until
// the user picks; the pick becomes a visible mix.downmix node in that mode.
// Positioned at the destination port, in screen space (fixed), so the
// canvas's pan/zoom transform never applies to it. Escape or an outside
// click cancels the connection.
import { useEffect, useRef } from "react";
import { useAutoFlipPosition } from "./useAutoFlipPosition";
import {
  resolveConnectionChoice,
  type PendingConnectionChoice,
} from "./graphStore";
import "./NodeContextMenu.css";

const CHOICE_LABELS: Record<string, string> = {
  mid: "Mid (L+R)",
  left: "Left",
  right: "Right",
  side: "Side (L−R)",
};

function portScreenPosition(pending: PendingConnectionChoice): {
  x: number;
  y: number;
} {
  const selector = `[data-node-id="${CSS.escape(pending.toNodeId)}"][data-port-id="${CSS.escape(pending.toPortId)}"]`;
  const element = document.querySelector(selector);
  if (!element) return { x: window.innerWidth / 2, y: window.innerHeight / 2 };
  const rect = element.getBoundingClientRect();
  return { x: rect.right + 8, y: rect.top };
}

export function ConnectionChoiceMenu({
  pending,
}: {
  pending: PendingConnectionChoice;
}) {
  const rootRef = useRef<HTMLDivElement | null>(null);
  const anchor = portScreenPosition(pending);
  const pos = useAutoFlipPosition(anchor.x, anchor.y, rootRef);

  useEffect(() => {
    const onPointerDown = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node))
        resolveConnectionChoice(null);
    };
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Escape") resolveConnectionChoice(null);
    };
    window.addEventListener("mousedown", onPointerDown, true);
    window.addEventListener("keydown", onKeyDown);
    return () => {
      window.removeEventListener("mousedown", onPointerDown, true);
      window.removeEventListener("keydown", onKeyDown);
    };
  }, []);

  return (
    <div
      ref={rootRef}
      className="node-context-menu"
      style={{ left: pos.left, top: pos.top }}
    >
      <div className="node-context-menu-title">Stereo → mono</div>
      {pending.choices.map((choice) => (
        <button
          key={choice}
          type="button"
          onClick={() => resolveConnectionChoice(choice)}
        >
          {CHOICE_LABELS[choice] ?? choice}
        </button>
      ))}
    </div>
  );
}
