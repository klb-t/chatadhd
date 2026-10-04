import type { LoomApi } from "./loom-api";
import { LoomHttpApi } from "./loom-http";
import { LoomJniApi, hasNativeBridge } from "./loom-jni";

export const api: LoomApi = hasNativeBridge() ? new LoomJniApi() : new LoomHttpApi();

export * from "./loom-api";
export * from "./types";
export * from "./graph-packets";
