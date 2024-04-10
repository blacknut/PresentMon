// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
import { RgbaColor } from './color'
import { QualifiedMetric, makeDefaultQualifiedMetric } from './qualified-metric';

export enum AxisAffinity {
    Left,
    Right,
}

// contains information describing one of possibly many metrics contained in a widget
// along with per-metric annotation/presentation data such as colors and alignment
export interface WidgetMetric {
    key: number;
    metric: QualifiedMetric;
    lineColor: RgbaColor;
    fillColor: RgbaColor;
    axisAffinity: AxisAffinity;
}

let nextKey = 0;

function GenerateKey(): number {
    return nextKey++;
}

export function ResetKeySequence(): void {
    nextKey = 0;
}

export function RegenerateKeys(m: WidgetMetric): void {
    m.key = GenerateKey();
}

export function makeDefaultWidgetMetric(qualifiedMetric: QualifiedMetric|null = null): WidgetMetric {
    return {
        key: GenerateKey(),
        metric: qualifiedMetric ?? makeDefaultQualifiedMetric(),
        lineColor: {
          r: 100,
          g: 255,
          b: 255,
          a: 220 / 255,
        },
        fillColor: {
          r: 57,
          g: 210,
          b: 250,
          a: 25 / 255,
        },
        axisAffinity: AxisAffinity.Left,
    };
}
