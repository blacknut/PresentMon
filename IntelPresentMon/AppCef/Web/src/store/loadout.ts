// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
import { Module, VuexModule, Mutation, Action, getModule  } from 'vuex-module-decorators'
import store from './index'
import { Graph, makeDefaultGraph } from '@/core/graph'
import { Readout } from '@/core/readout'
import { makeDefaultReadout } from '@/core/readout'
import { AsGraph, AsReadout, Widget, WidgetType, GenerateKey, ResetKeySequence } from '@/core/widget'
import { Introspection } from './introspection'
import { WidgetMetric, makeDefaultWidgetMetric } from '@/core/widget-metric'
import { LoadoutFile } from '@/core/loadout'
import { Preferences } from './preferences'
import { signature } from '@/core/loadout'
import { Api } from '@/core/api'
import { Preset } from '@/core/preferences'
import { migrateLoadout } from '@/core/loadout-migration'
import { QualifiedMetric } from '@/core/qualified-metric'
import { Notifications } from './notifications'

@Module({name: 'loadout', dynamic: true, store, namespaced: true})
export class LoadoutModule extends VuexModule {
    widgets: Widget[] = [];

    debounceToken: number|null = null;

    @Mutation
    setDebounceToken(token: number|null) {
        this.debounceToken = token;
    }

    get fileContents(): string {
        const file: LoadoutFile = {
            signature,
            widgets: this.widgets,
        };
        return JSON.stringify(file, null, 3);
    }

    @Action({rawError: true})
    async browseAndSerialize() {
        await Api.browseStoreSpec(this.fileContents);
    }

    @Action({rawError: true})
    async serializeCustom() {
        // make sure we are on custom preset
        if (Preferences.preferences.selectedPreset === Preset.Custom) {
            if (this.debounceToken !== null) {
                clearTimeout(this.debounceToken);
            }
            const token = setTimeout(() => {
                this.setDebounceToken(null);
                Api.storeConfig(this.fileContents, 'custom-auto.json');
            }, 400);
            this.setDebounceToken(token);
        }
    }

    @Mutation
    addGraph_() {
        const qualifiedMetric: QualifiedMetric = {
            metricId: 8,
            arrayIndex: 0,
            statId: 1,
            deviceId: 0,
            desiredUnitId: 0
        };
        this.widgets.push(makeDefaultGraph(qualifiedMetric));
    }
    @Action({rawError: true})
    async addGraph() {
        this.context.commit('addGraph_');
        await this.serializeCustom();
    }
    @Mutation
    setGraphAttribute_<K extends keyof Graph>(payload: {index:number, attr: K, val: Graph[K]}) {
        AsGraph(this.widgets[payload.index])[payload.attr] = payload.val;
    }
    @Action({rawError: true})
    async setGraphAttribute<K extends keyof Graph>(payload: {index:number, attr: K, val: Graph[K]}) {
        this.context.commit('setGraphAttribute_', payload);
        await this.serializeCustom();
    }
    @Mutation
    setGraphTypeAttribute_<K extends keyof Graph['graphType']>(payload: {index:number, attr: K, val: Graph['graphType'][K]}) {
        AsGraph(this.widgets[payload.index]).graphType[payload.attr] = payload.val;
    }
    @Action({rawError: true})
    async setGraphTypeAttribute<K extends keyof Graph['graphType']>(payload: {index:number, attr: K, val: Graph['graphType'][K]}) {
        this.context.commit('setGraphTypeAttribute_', payload);
        await this.serializeCustom();
    }

    @Mutation
    addReadout_() {
        const metric = Introspection.metrics[0];
        const qualifiedMetric: QualifiedMetric = {
            metricId: metric.id,
            arrayIndex: 0,
            statId: metric.availableStatIds[0],
            deviceId: 0,
            desiredUnitId: 0
        };
        this.widgets.push(makeDefaultReadout(qualifiedMetric));
    }
    @Action({rawError: true})
    async addReadout() {
        this.context.commit('addReadout_');
        await this.serializeCustom();
    }
    @Mutation
    setReadoutAttribute_<K extends keyof Readout>(payload: {index:number, attr: K, val: Readout[K]}) {
        AsReadout(this.widgets[payload.index])[payload.attr] = payload.val;
    }
    @Action({rawError: true})
    async setReadoutAttribute<K extends keyof Readout>(payload: {index:number, attr: K, val: Readout[K]}) {
        this.context.commit('setReadoutAttribute_', payload);
        await this.serializeCustom();
    }

    
    @Mutation
    removeWidget_(index:number) {
        this.widgets.splice(index, 1);
    }
    @Action({rawError: true})
    async removeWidget(index:number) {
        this.context.commit('removeWidget_', index);
        await this.serializeCustom();
    }
    @Mutation
    setWidgetMetrics_(payload: {index:number, metrics: WidgetMetric[]}) {
        const widget = this.widgets[payload.index];
        if (widget.widgetType !== WidgetType.Graph || (widget as Graph).graphType.name !== 'Line') {
            if (payload.metrics.length > 1) {
                console.warn(`Widget #${payload.index} is not Line Graph but trying to set ${payload.metrics.length} metrics`);
                widget.metrics= [payload.metrics[0]];
            }
        }
        this.widgets[payload.index].metrics = payload.metrics;
    }
    @Action({rawError: true})
    async setWidgetMetrics(payload: {index:number, metrics: WidgetMetric[]}) {
        this.context.commit('setWidgetMetrics_', payload);
        await this.serializeCustom();
    }
    @Mutation
    addWidgetMetric_(payload: {index:number, metric: QualifiedMetric}) {
        const widget = this.widgets[payload.index];
        if (widget.widgetType !== WidgetType.Graph || (widget as Graph).graphType.name !== 'Line') {
            console.warn(`Widget #${payload.index} is not Line Graph but trying to add metric`);
            throw new Error('bad addition of metric to widget');
        }
        widget.metrics.push(makeDefaultWidgetMetric(payload.metric));
    }
    @Action({rawError: true})
    async addWidgetMetric(payload: {index:number, metric: QualifiedMetric|null}) {
        this.context.commit('addWidgetMetric_', payload);
        await this.serializeCustom();
    }
    @Mutation
    removeWidgetMetric_(payload: {index:number, metricIdIdx: number}) {
        const widget = this.widgets[payload.index];
        if (widget.metrics.length < 2) {
            console.warn('Not enough metrics in widget to allow a remove operation');
            return;
        }
        widget.metrics.splice(payload.metricIdIdx, 1);
    }
    @Action({rawError: true})
    async removeWidgetMetric(payload: {index:number, metricIdIdx: number}) {
        this.context.commit('removeWidgetMetric_', payload);
        await this.serializeCustom();
    }
    @Mutation
    setWidgetMetric_(payload: {index:number, metricIdx: number, metric: WidgetMetric}) {
        const widget = this.widgets[payload.index];
        widget.metrics.splice(payload.metricIdx, 1, payload.metric);
    }
    @Action({rawError: true})
    async setWidgetMetric(payload: {index:number, metricIdx: number, metric: WidgetMetric}) {
        this.context.commit('setWidgetMetric_', payload);
        await this.serializeCustom();
    }
    @Mutation
    resetWidgetAs_(payload: {index: number, type: WidgetType}) {
        let qualifiedMetric:QualifiedMetric|null = this.widgets[payload.index].metrics[0].metric;
        // we need to change the metric ID if we're resetting as Graph and metric is not numeric
        if (qualifiedMetric !== null && payload.type === WidgetType.Graph) {
            const metric = Introspection.metrics.find(m => m.id === qualifiedMetric!.metricId)
            if (!metric || !metric.numeric) {
                qualifiedMetric = null;
            }
        }
        let w: Widget;
        switch (payload.type) {
            case WidgetType.Graph: w = makeDefaultGraph(qualifiedMetric); break;
            case WidgetType.Readout: w = makeDefaultReadout(qualifiedMetric); break;
        }
        this.widgets.splice(payload.index, 1, w);
    }
    @Action({rawError: true})
    async resetWidgetAs(payload: {index: number, type: WidgetType}) {
        this.context.commit('resetWidgetAs_', payload);
        await this.serializeCustom();
    }
    @Mutation
    moveWidget_(payload: {from: number, to: number}) {
        const movedItem = this.widgets.splice(payload.from, 1)[0];
        this.widgets.splice(payload.to, 0, movedItem);
    }
    @Action({rawError: true})
    async moveWidget(payload: {from: number, to: number}) {
        this.context.commit('moveWidget_', payload);
        await this.serializeCustom();
    }

    @Mutation
    replaceWidgets_(widgets: Widget[]) {
        // reset widget keys
        ResetKeySequence();
        for (const w of widgets) {
            if (!Introspection.metrics.some(avail => w.metrics.some(req => avail.id === req.metric.metricId))) {
                throw new Error('Unknown metric ID encountered');
            }
            w.key = GenerateKey();
        }
        this.widgets.splice(0, this.widgets.length, ...widgets);
    }

    // call loadConfigFromPayload instead of this unless you know what you're doing
    @Action({rawError: true})
    async parseAndReplace(payload: {payload: string}) {
        // parse string to js object
        const loadout = JSON.parse(payload.payload) as LoadoutFile;
        // handle versioning
        if (loadout.signature.code !== signature.code) throw new Error(`Bad loadout file format; expect:${signature.code} actual:${loadout.signature.code}`);
        if (loadout.signature.version !== signature.version) {
            migrateLoadout(loadout);
            console.info(`loadout migrated to ${signature.version}`);
        }
        // remove unsupported metrics from widgets
        const options = Introspection.metricOptions;
        for (const w of loadout.widgets) {
            w.metrics = w.metrics.filter(m => options.find(o =>
                o.metricId === m.metric.metricId &&
                o.arrayIndex === m.metric.arrayIndex
            ) !== undefined);
        }
        // remove empty widgets
        // possible corner case: can get empty loadout if original only includes non-option metrics
        loadout.widgets = loadout.widgets.filter(w => w.metrics.length > 0);
        // commit the mutation to replace widgets
        this.context.commit('replaceWidgets_', loadout.widgets);
    }

    // wraps exceptions to properly notify of migration issues
    @Action({rawError: true})
    async loadConfigFromPayload(pay: {payload: string, err: string}) {
        let {payload, err} = pay;
        try {
          await Loadout.parseAndReplace({ payload });
        }
        catch (e: any) {
          if (e.noticeOverride) {
            err += e.message ?? '';
          }
          await Notifications.notify({text: err});
          console.error([err, e]);
        }
    }
}

export const Loadout = getModule(LoadoutModule);