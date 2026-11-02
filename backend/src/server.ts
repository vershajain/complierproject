import "dotenv/config";
import cors from "cors";
import express from "express";
import { analyzeRouter } from "./routes/analyze";
import { aiFixRouter } from "./routes/aiFix";
import { ANALYZER_PATH, analyzerExists } from "./services/analyzerRunner";
import { isAiConfigured } from "./services/aiService";

const app = express();
app.use(cors());
app.use(express.json({ limit: "1mb" }));

app.get("/api/health", (_req, res) => {
  res.json({ ok: true, analyzerBuilt: analyzerExists(), aiConfigured: isAiConfigured() });
});
app.use("/api/analyze", analyzeRouter);
app.use("/api/ai-fix", aiFixRouter);

const port = Number(process.env.PORT) || 5000;
app.listen(port, () => {
  console.log(`C-Lint backend running at http://localhost:${port}`);
  console.log(`Analyzer: ${ANALYZER_PATH} ${analyzerExists() ? "(found)" : "(NOT FOUND - run: npm run build:analyzer)"}`);
});
