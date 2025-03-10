/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Chart from 'chart.js'
import 'chartjs-plugin-trendline'
Chart.defaults.global.defaultFontFamily = "'azo-sans-web', adrianna, serif"
Chart.defaults.global.defaultFontColor = '#424F62'
Chart.defaults.global.defaultFontSize = 10

Chart.defaults.scale.gridLines.lineWidth = 0.6
Chart.defaults.scale.gridLines.color = 'rgba(234, 234, 234, 1)'
Chart.defaults.scale.gridLines.drawTicks = false
Chart.defaults.scale.gridLines.drawBorder = true
Chart.defaults.scale.gridLines.zeroLineColor = 'rgba(234, 234, 234, 1)'
Chart.defaults.scale.ticks.padding = 12

/**
 * show tooltip at cursor position.
 * tooltips.position = 'cursor'
 */
Chart.Tooltip.positioners.cursor = (_, coordinates) => coordinates
