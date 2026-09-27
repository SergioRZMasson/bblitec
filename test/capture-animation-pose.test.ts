import assert from "node:assert/strict";
import test from "node:test";
import * as pin from "@babylonjs/lite";
import { applyCaptureAnimationPose } from "../src/capture-animation-pose.js";
import {
    suiteBrowserModule,
    suiteBrowserModuleDigest,
} from "../src/capture-suite-reference.js";
import { captureMetaStaleness } from "../src/tooling/artifacts.js";
import { getScene } from "../src/scene-registry.js";
import { createJavaScriptFunction } from "../src/typescript-transpile.js";

function propertyGroup(
    manager: pin.AnimationManager,
    target: { value: number },
    frameRate: number,
    end: number,
): pin.AnimationGroup {
    const clip = pin.createPropertyAnimationClip(
        `value-${frameRate}-${end}`,
        [
            {
                path: "value",
                keys: [
                    { frame: 0, value: 0 },
                    { frame: frameRate * 2, value: end },
                ],
            },
        ],
        { frameRate },
    );
    return pin.createPropertyAnimationGroup(manager, target, clip);
}

test("fresh pose applies each pinned property controller at its own frame rate", () => {
    const manager = pin.createAnimationManager();
    const first = { value: -100 };
    const second = { value: -200 };
    const groups = [
        propertyGroup(manager, first, 10, 20),
        propertyGroup(manager, second, 12, 40),
    ];
    for (const group of groups) {
        group.currentTime = 0.75;
        pin.pauseAnimation(group);
    }
    pin.updateAnimationManager(manager, 0);
    assert.deepEqual(
        [first.value, second.value],
        [-100, -200],
        "legacy write-and-pause does not evaluate property controllers",
    );
    applyCaptureAnimationPose(groups, 0.75, undefined, pin.goToFrame);
    assert.deepEqual([first.value, second.value], [7.5, 15]);
    assert(
        groups.every((group) => !group.isPlaying && group.currentTime === 0.75),
    );
});

test("fresh pose preserves manager registration order and stopped groups", () => {
    const manager = pin.createAnimationManager();
    const target = { value: 0 };
    const first = propertyGroup(manager, target, 10, 6);
    const second = propertyGroup(manager, target, 12, 14);
    const stopped = propertyGroup(manager, target, 10, 200);
    pin.stopAnimation(stopped);
    applyCaptureAnimationPose(
        [stopped, second, first, second],
        1,
        undefined,
        pin.goToFrame,
    );
    assert.equal(target.value, 7);
    assert.equal(stopped.currentTime, 0);
    assert.equal(stopped.isPlaying, false);
    pin.updateAnimationManager(manager, 100);
    assert.equal(target.value, 7);
});

test("fresh pose restores actual pinned blending without ticking fixed steps or custom tasks", () => {
    let unrelatedTicks = 0;
    let preUpdates = 0;
    const manager = Object.assign(
        pin.createAnimationManager({ fixedDeltaMs: 500 }),
        {
            _preUpdate: () => {
                preUpdates++;
            },
        },
    );
    pin.addAnimationTask(
        manager,
        pin.createAnimationTask(() => {
            unrelatedTicks++;
        }),
    );
    const target = { value: 0 };
    const first = propertyGroup(manager, target, 10, 6);
    const second = propertyGroup(manager, target, 12, 14);
    pin.setAnimationWeight(first, 0.25);
    pin.setAnimationWeight(second, 0.75);
    pin.enablePropertyAnimationBlending(manager);
    applyCaptureAnimationPose([second, first], 1, undefined, pin.goToFrame);
    assert.equal(
        target.value,
        6,
        "weighted 3*0.25 + 7*0.75, not the last writer",
    );
    assert.deepEqual([first.currentTime, second.currentTime], [1, 1]);
    assert.equal(manager.fixedDeltaMs, 500);
    assert.equal(unrelatedTicks, 0);
    assert.equal(preUpdates, 0);
});

test("fresh pose forwards the owning engine and otherwise the scene engine", () => {
    // Property controllers do not access a GPU. These identity-only engine
    // records exercise the handoff without constructing a rendering device.
    const owner = Object.assign(pin.createAnimationManager(), {
        engine: { label: "owner" },
    });
    const scene = Object.assign(pin.createAnimationManager(), {
        engine: { label: "scene" },
    });
    const owned = propertyGroup(owner, { value: 0 }, 10, 10);
    const ordinary = propertyGroup(
        pin.createAnimationManager(),
        { value: 0 },
        10,
        10,
    );
    const engines: Array<pin.EngineContext | undefined> = [];
    for (const selected of [owned, ordinary]) {
        applyCaptureAnimationPose(
            [selected],
            1,
            scene.engine,
            (group, frame, engine) => {
                engines.push(engine);
                pin.goToFrame(group, frame, engine);
            },
        );
    }
    assert.deepEqual(engines, [owner.engine, scene.engine]);
});

test("fresh pose refuses unknown cross-owner scheduling before changing any group", () => {
    const target = { value: -1 };
    const first = propertyGroup(pin.createAnimationManager(), target, 10, 6);
    const secondOwner = pin.createAnimationManager();
    const second = propertyGroup(secondOwner, target, 10, 14);
    assert.throws(
        () =>
            applyCaptureAnimationPose(
                [first, second],
                1,
                undefined,
                pin.goToFrame,
            ),
        /distinct owners/,
    );
    assert.equal(target.value, -1);
    assert.deepEqual([first.currentTime, second.currentTime], [0, 0]);
    assert(first.isPlaying && second.isPlaying);
    pin.removeAnimationGroup(secondOwner, second);
    assert.throws(
        () =>
            applyCaptureAnimationPose(
                [first, second],
                1,
                undefined,
                pin.goToFrame,
            ),
        /distinct owners/,
    );
    assert.equal(target.value, -1);
});

test("fresh pose refuses multiple unowned groups whose scene tick order is unknown", () => {
    const manager = pin.createAnimationManager();
    const target = { value: -1 };
    const first = propertyGroup(manager, target, 10, 6);
    const second = propertyGroup(manager, target, 10, 14);
    pin.removeAnimationGroup(manager, first);
    pin.removeAnimationGroup(manager, second);
    assert.throws(
        () =>
            applyCaptureAnimationPose(
                [second, first],
                1,
                undefined,
                pin.goToFrame,
            ),
        /multiple unowned/,
    );
    assert.equal(target.value, -1);
    assert.deepEqual([first.currentTime, second.currentTime], [0, 0]);
    applyCaptureAnimationPose([first], 1, undefined, pin.goToFrame);
    assert.equal(target.value, 3);
});

test("fresh pose validates the entire selection before changing a target", () => {
    const manager = pin.createAnimationManager();
    const target = { value: -1 };
    const first = propertyGroup(manager, target, 10, 6);
    const second = propertyGroup(manager, target, 12, 14);
    pin.enablePropertyAnimationBlending(manager);
    assert.throws(
        () => applyCaptureAnimationPose([first], 1, undefined, pin.goToFrame),
        /every active blended/,
    );
    assert.equal(target.value, -1);
    assert.equal(first.isPlaying, true);
    pin.setAnimationTaskCategoryHandler(manager, "unrelated", () => true);
    assert.throws(
        () =>
            applyCaptureAnimationPose(
                [first, second],
                1,
                undefined,
                pin.goToFrame,
            ),
        /animation category/,
    );
    assert.equal(target.value, -1);
    pin.enablePropertyAnimationBlending(manager);
    Object.assign(second, { _gltfMixer: [] });
    assert.throws(
        () =>
            applyCaptureAnimationPose(
                [first, second],
                1,
                undefined,
                pin.goToFrame,
            ),
        /manager's engine/,
    );
    assert.equal(target.value, -1);
    const inconsistent = Object.assign(first, {
        _animationManager: pin.createAnimationManager(),
    });
    assert.throws(
        () =>
            applyCaptureAnimationPose(
                [inconsistent],
                1,
                undefined,
                pin.goToFrame,
            ),
        /ownership is inconsistent/,
    );
    assert.equal(target.value, -1);
});

test("serialized fresh pose uploads the actual pinned weighted glTF palette before any manager tick", () => {
    // The fixture supplies the GPU queue boundary; every controller, sampler,
    // manager and blend operation is the real pinned package implementation.
    const result = createJavaScriptFunction(
        "pin",
        `
const apply = ${applyCaptureAnimationPose.toString()};
const identity = new Float32Array([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]);
const uploads = [];
const ownerEngine = { _device: { queue: { writeTexture: (destination, buffer) => {
    uploads.push([...new Float32Array(buffer)]);
} } } };
const sceneEngine = { _device: { queue: { writeTexture: () => {
    throw new Error("Wrong engine used for owned glTF controller.");
} } } };
const nodes = [{ parentIdx:-1, tx:0, ty:0, tz:0, rx:0, ry:0, rz:0, rw:1, sx:1, sy:1, sz:1 }];
const skeletons = [{ boneCount:1, jointNodes:[0], invMeshWorld:identity,
    inverseBindMatrices:identity, boneMatrices:new Float32Array(16), boneTexture:{} }];
const clip = (name, frameRate, end) => ({ name, duration:2, frameRate,
    channels:[{ nodeIdx:0, samplerIdx:0, path:0 }],
    samplers:[{ input:new Float32Array([0,2]), output:new Float32Array([0,0,0,end,0,0]), interpolation:0 }],
});
const groups = pin.createAnimationGroups({ clips:[clip("first",10,6),clip("second",12,14),clip("stopped",30,200)],
    nodes, skeletons, morphBindings:[], nodeTargets:[], nodeNames:["joint"] });
const manager = pin.createAnimationManager({ engine:ownerEngine });
pin.addAnimationGroups(manager,groups);
pin.playAnimation(groups[1]);
pin.setAnimationWeight(groups[0],0.25);
pin.setAnimationWeight(groups[1],0.75);
pin.enableAnimationBlending(manager);
apply([...groups].reverse(),1,sceneEngine,pin.goToFrame);
return { palettes:uploads.map(bytes => bytes[12]), final:[...skeletons[0].boneMatrices],
    times:groups.map(group => group.currentTime), stopped:groups.map(group => !!group._stopped),
    playing:groups.map(group => group.isPlaying) };
`,
    )(pin);
    assert.deepEqual(result, {
        palettes: [-3, -7, -6],
        final: [-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -6, 0, 0, 1],
        times: [1, 1, 0],
        stopped: [false, false, true],
        playing: [false, false, false],
    });
});

test("fresh pose has distinct module provenance which legacy evidence readers refuse", () => {
    const scene = getScene("scene150");
    const time = scene.parity!.referenceTimeSeconds!;
    const groups = scene.parity!.referenceAnimationGroups;
    const legacy = suiteBrowserModuleDigest(scene.source, time, groups);
    const fresh = suiteBrowserModuleDigest(
        scene.source,
        time,
        groups,
        undefined,
        undefined,
        "applied-group-pose-v1",
    );
    assert.notEqual(fresh, legacy);
    assert.equal(suiteBrowserModuleDigest(scene.source, time, groups), legacy);
    assert.match(
        suiteBrowserModule(
            scene.source,
            undefined,
            time,
            groups,
            undefined,
            undefined,
            "applied-group-pose-v1",
        ),
        /animationPoseProtocol = "applied-group-pose-v1"/,
    );
    assert.match(
        captureMetaStaleness(
            { seekSeconds: time, pin: "same-pin", moduleSha256: fresh },
            {
                pin: "same-pin",
                moduleSha256: () => legacy,
            },
        )!,
        /different scene module/,
    );
    assert.throws(
        () =>
            suiteBrowserModule(
                scene.source,
                undefined,
                undefined,
                groups,
                undefined,
                undefined,
                "applied-group-pose-v1",
            ),
        /nonnegative pose/,
    );
    assert.throws(
        () =>
            suiteBrowserModule(
                scene.source,
                () => "export {};",
                time,
                groups,
                undefined,
                undefined,
                "applied-group-pose-v1",
            ),
        /registration anchor/,
    );
    assert.throws(
        () =>
            suiteBrowserModule(
                scene.source,
                undefined,
                time,
                groups,
                undefined,
                2,
                "applied-group-pose-v1",
            ),
        /one local scene engine/,
    );
});
