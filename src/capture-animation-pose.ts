import type {
    AnimationGroup,
    AnimationManager,
    EngineContext,
    goToFrame,
} from "@babylonjs/lite";

/** Internal fields from the pinned animation-group/task/manager sources. */
interface CaptureAnimationGroup extends AnimationGroup {
    _stopped?: boolean;
    _gltfMixer?: unknown;
    _animationManager?: CaptureAnimationManager;
}

interface CaptureAnimationManager extends AnimationManager {
    _animationGroups?: CaptureAnimationGroup[];
    _taskCategory?: string;
    _taskCategoryHandler?: (
        manager: AnimationManager,
        deltaMs: number,
    ) => boolean;
}

/**
 * A pose-only operation: pinned seeks apply ordinary controllers, then the
 * owner's category handler restores its blended result. Do not call the whole
 * manager update: fixed steps, fades and unrelated tasks must not advance.
 * Self-contained because the browser composer embeds this function's source.
 */
export function applyCaptureAnimationPose(
    groups: readonly CaptureAnimationGroup[],
    seconds: number,
    engine: EngineContext | undefined,
    seek: typeof goToFrame,
): void {
    if (!Number.isFinite(seconds) || seconds < 0)
        throw new Error("Capture pose must be finite and nonnegative.");
    const selected = new Set(groups);
    const active = [...selected].filter((group) => !group._stopped);
    const owners = new Set(active.map((group) => group._animationManager));
    if (owners.size > 1)
        throw new Error(
            "Capture cannot order animation groups with distinct owners.",
        );
    if (owners.has(undefined) && active.length > 1)
        throw new Error(
            "Capture cannot order multiple unowned animation groups.",
        );
    const managers = new Set<CaptureAnimationManager>();
    for (const group of selected) {
        const frameRate = group.frameRate || 60;
        if (!Number.isFinite(frameRate) || frameRate <= 0)
            throw new Error("Capture animation frame rate is invalid.");
        const owner = group._animationManager;
        if (!owner) continue;
        if (!owner._animationGroups?.includes(group))
            throw new Error(
                "Capture animation manager ownership is inconsistent.",
            );
        if (!owner._taskCategoryHandler || managers.has(owner)) continue;
        if (owner._taskCategory !== "animation-group")
            throw new Error("Capture cannot evaluate this animation category.");
        for (const sibling of owner._animationGroups) {
            if (sibling._animationManager !== owner)
                throw new Error(
                    "Capture animation manager ownership is inconsistent.",
                );
            if (sibling._stopped) continue;
            if (!selected.has(sibling))
                throw new Error(
                    "Capture must select every active blended animation group.",
                );
            if (sibling._gltfMixer && !owner.engine)
                throw new Error(
                    "Capture glTF blending requires its manager's engine.",
                );
        }
        managers.add(owner);
    }
    // Complete validation before changing a controller or any animated target.
    const ordered: CaptureAnimationGroup[] = [];
    const seen = new Set<CaptureAnimationGroup>();
    for (const group of selected) {
        if (seen.has(group)) continue;
        for (const sibling of group._animationManager?._animationGroups ?? [
            group,
        ]) {
            if (!selected.has(sibling) || seen.has(sibling)) continue;
            seen.add(sibling);
            ordered.push(sibling);
        }
    }
    for (const group of ordered) {
        if (group._stopped) continue;
        seek(
            group,
            seconds * (group.frameRate || 60),
            group._animationManager?.engine ?? engine,
        );
    }
    for (const manager of managers) manager._taskCategoryHandler?.(manager, 0);
}
