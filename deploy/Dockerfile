FROM node:24.18.0-alpine AS web
WORKDIR /web
COPY web/package.json web/package-lock.json ./
RUN npm ci --ignore-scripts --no-audit --no-fund
COPY web/ ./
RUN npm run build

FROM golang:1.27.3-alpine AS build
WORKDIR /src
COPY server/ ./
COPY --from=web /web/dist/ ./ui/
RUN CGO_ENABLED=0 go test ./... && CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /nimbo-server .

FROM scratch
COPY --from=build /nimbo-server /nimbo-server
USER 65532:65532
ENTRYPOINT ["/nimbo-server"]
