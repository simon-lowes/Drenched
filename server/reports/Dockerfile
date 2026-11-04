# The report relay (relay.mjs). Build from server/: docker build -f reports/Dockerfile .
FROM node:22-alpine
WORKDIR /app
COPY src/reports.js src/reports.js
COPY reports/relay.mjs reports/relay.mjs
RUN echo '{"type":"module"}' > package.json
USER node
ENV PORT=8080
EXPOSE 8080
CMD ["node", "reports/relay.mjs"]
