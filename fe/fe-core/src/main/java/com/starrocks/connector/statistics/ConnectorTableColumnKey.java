// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


package com.starrocks.connector.statistics;

import java.util.Objects;

public class ConnectorTableColumnKey {
    public final String tableUUID;
    public final String column;

    public ConnectorTableColumnKey(String tableUUID, String column) {
        this.tableUUID = tableUUID;
        this.column = column;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (o == null || getClass() != o.getClass()) {
            return false;
        }

        ConnectorTableColumnKey
                cacheKey = (ConnectorTableColumnKey) o;

        if (!tableUUID.equals(cacheKey.tableUUID)) {
            return false;
        }
        return column.equalsIgnoreCase(cacheKey.column);
    }

    @Override
    public int hashCode() {
        return Objects.hash(tableUUID, column);
    }
}