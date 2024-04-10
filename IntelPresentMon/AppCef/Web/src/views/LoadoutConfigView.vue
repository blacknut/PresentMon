<!-- Copyright (C) 2022 Intel Corporation -->
<!-- SPDX-License-Identifier: MIT -->

<template>
  <div class="flex-grow-1 flex-column mx-lg-12 mx-xl-16">
    
    <h2 class="mt-5 ml-5 link-head" @click="$router.back()">
        <v-icon style="vertical-align: 0" color="inherit">mdi-chevron-left</v-icon>
        Loadout Configuration
    </h2>

    <v-row class="mt-5 loadout-table" id="sortable-row-container">
      <loadout-row
        v-for="(w, i) in widgets" :key="w.key" :stats="stats"
        :widgetIdx="i" :widgets="widgets" :metrics="metrics" 
        :metricOptions="metricOptions" :adapterId="activeAdapterId" :locked="false" 
        @delete="removeWidget" 
      ></loadout-row>
      <div class="add-btn-row">
        <v-btn @click="addWidget()" class="add-btn">Add New Widget</v-btn>
      </div>
    </v-row>

    <v-row>
      <v-col cols="6" style="text-align: center"><v-btn @click="save()">Save</v-btn></v-col>
      <v-col cols="6" style="text-align: center"><v-btn @click="load()">Load</v-btn></v-col>
    </v-row>
  </div>
</template>

<script lang="ts">
import Vue from 'vue'
import Sortable from 'sortablejs'
import { Preferences } from '@/store/preferences'
import { Preferences as PrefType } from '@/core/preferences'
import { Loadout } from '@/store/loadout'
import { Introspection } from '@/store/introspection'
import { Metric } from '@/core/metric'
import { Stat } from '@/core/stat'
import { Widget } from '@/core/widget'
import { Api } from '@/core/api'
import { Notifications } from '@/store/notifications'
import LoadoutRow from '@/components/LoadoutRow.vue'
import { MetricOption } from '@/core/metric-option'

export default Vue.extend({
  name: 'LoadoutConfig',
  components: {
    LoadoutRow,
  },

  data: () => ({
  }),  

  mounted() {
    new Sortable(document.querySelector('#sortable-row-container')!, {
      draggable: '.sortable-row',
      handle: '.sortable-handle',
      forceFallback: true,
      onChoose: e => e.target.classList.add('sortable-grabbing'),
      onUnchoose: e => e.target.classList.remove('sortable-grabbing'),
      onStart: e => e.target.classList.add('sortable-grabbing'),
      onEnd: e => {
          e.target.classList.remove('sortable-grabbing');
          this.dragReorder(e);
      },
    });
  },
  methods: {
    async save() {
      try {
        await Loadout.browseAndSerialize();
      } catch (e) {
        await Notifications.notify({text: 'Failed to save loadout.'});
      }
    },
    async load() {
      const {payload} = await Api.browseReadSpec();
      // zero length result means user canceled out of dialog
      if (payload.length > 0) {
        const err = 'Failed to load preset file. ';
        await Loadout.loadConfigFromPayload({ payload, err });
        await Loadout.serializeCustom();
      }
    },
    addWidget() {
      Loadout.addGraph();
    },
    removeWidget(widgetIdx: number) {
      Loadout.removeWidget(widgetIdx);
    },
    dragReorder(e: Sortable.SortableEvent) {
      if (e.oldIndex !== undefined && e.newIndex !== undefined) {
        Loadout.moveWidget({from: e.oldIndex, to: e.newIndex});
      }
    }
  },  
  computed: {
    pref(): PrefType {
      return Preferences.preferences;
    },
    activeAdapterId(): number|null {
      return this.pref.adapterId;
    },
    metrics(): Metric[] {
      return Introspection.metrics;
    },
    stats(): Stat[] {
      return Introspection.stats;
    },
    widgets(): Widget[] {
      return Loadout.widgets;
    },
    metricOptions(): MetricOption[] {
      return Introspection.metricOptions;
    },
    selectedPreset: {
      set(preset: number|null) {
        Preferences.writeAttribute({attr: 'selectedPreset', val: preset});
      },
      get(): number|null {
        return Preferences.preferences.selectedPreset;
      }
    },
  },
});
</script>

<style lang="scss">
.loadout-table { 
  user-select: none; 
  .sortable-handle {
    cursor: grab;
  }
  .sortable-handle:active {
    cursor: grabbing !important;
  }
  .sortable-ghost {
      background-color: #444 !important;
  }
  .widget-btn, .line-btn {
    visibility: hidden;
    opacity: 0.4;
  }
  .widget-row:hover .widget-btn {
    visibility: visible;
  }
  .widget-row:hover .widget-btn.remove-btn:hover {
    opacity: 1;
    color: red;
  }
  .widget-row:hover .widget-btn.add-line-btn:hover {
    opacity: 1;
    color: greenyellow;
  }
  .widget-row:hover .widget-btn.details-btn:hover {
    opacity: 1;
    color: lightskyblue;
  }
}
.widget-row {
  width: 100%;
  display: flex;
  margin: 3px;
  border: 1px solid rgb(32, 32, 32);
  border-radius: 3px;
  background-color: rgb(26, 26, 26);
}
.grip-wrap {
  flex-grow: 0;
  align-self: center;
  padding-left: 2px;
}
.line-wrap {
  flex-grow: 1;
}
.widget-line {
  display: flex;
  width: 100%;
  &:hover .line-btn {    
    visibility: visible;
  }
  &:hover .line-btn:hover {
    opacity: 1;
    color: red;
  }
}
.widget-row .widget-cell {
  margin: 2px;
  padding: 2px;
}
.widget-row .widget-cell.col-metric {
  flex: 3;
}
.widget-row .widget-cell.col-stat {
  width: 110px;
}
.widget-row .widget-cell.col-type {
  width: 120px;
}
.widget-row .widget-cell.col-subtype {
  width: 140px;
}
.widget-row .widget-cell.col-line-color {
  max-width: 50px;
  flex: 0.7;
  display: flex;
  flex-direction: column;
}
.widget-row .widget-cell.col-controls {
  width: 160px;
  vertical-align: middle;
}
.add-btn-row {
  width: 100%;
  display: flex;
  justify-content: center;
}
.sortable-grabbing * {
  cursor: grabbing !important;
}
button.v-btn.add-btn.add-btn {
  padding: 24px 96px;
  margin: 8px
}
.link-head { 
  color: white; 
  cursor: pointer; 
  user-select: none; 
  transition: color .2s; 
} 
.link-head:hover { 
  color: #2196f3; 
} 
</style>

